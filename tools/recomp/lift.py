"""Lifting of single ARM instructions to C statements over `NjCtx *c` (see runtime/backend/aot/recomp_rt.h).

Addresses are link addresses (the library is linked at 0); the guest address is `base + addr`, so reading PC
yields the constant `base + addr + 8`. Every handler returns a list of C statements; conditional instructions
are wrapped in `if (NJ_COND_xx(c)) { ... }`. Instructions without a handler raise Unsupported.
"""
from dataclasses import dataclass, field

from capstone import arm


class Unsupported(Exception):
    def __init__(self, mnemonic, op_str, why=""):
        super().__init__(f"{mnemonic} {op_str}" + (f" ({why})" if why else ""))
        self.mnemonic = mnemonic
        self.op_str = op_str


@dataclass
class LiftCtx:
    base: int
    fn: object = None
    plt: dict = field(default_factory=dict)
    overrides: set = field(default_factory=set)


REG_NUM = {f"r{i}": i for i in range(13)} | {"sb": 9, "sl": 10, "fp": 11, "ip": 12, "sp": 13, "lr": 14, "pc": 15}

COND = {arm.ARM_CC_EQ: "EQ", arm.ARM_CC_NE: "NE", arm.ARM_CC_HS: "HS", arm.ARM_CC_LO: "LO", arm.ARM_CC_MI: "MI",
        arm.ARM_CC_PL: "PL", arm.ARM_CC_VS: "VS", arm.ARM_CC_VC: "VC", arm.ARM_CC_HI: "HI", arm.ARM_CC_LS: "LS",
        arm.ARM_CC_GE: "GE", arm.ARM_CC_LT: "LT", arm.ARM_CC_GT: "GT", arm.ARM_CC_LE: "LE"}

SHIFT_KIND = {arm.ARM_SFT_LSL: "NJ_LSL", arm.ARM_SFT_LSR: "NJ_LSR", arm.ARM_SFT_ASR: "NJ_ASR",
              arm.ARM_SFT_ROR: "NJ_ROR", arm.ARM_SFT_RRX: "NJ_RRX",
              arm.ARM_SFT_LSL_REG: "NJ_LSL", arm.ARM_SFT_LSR_REG: "NJ_LSR", arm.ARM_SFT_ASR_REG: "NJ_ASR",
              arm.ARM_SFT_ROR_REG: "NJ_ROR", arm.ARM_SFT_RRX_REG: "NJ_RRX"}
REG_SHIFTS = {arm.ARM_SFT_LSL_REG, arm.ARM_SFT_LSR_REG, arm.ARM_SFT_ASR_REG, arm.ARM_SFT_ROR_REG,
              arm.ARM_SFT_RRX_REG}


def u32(v):
    return f"0x{v & 0xFFFFFFFF:08x}u"


class _Insn:
    """Per-instruction lifting helpers."""

    def __init__(self, ci, lc):
        self.ci = ci
        self.lc = lc
        self.word = int.from_bytes(ci.bytes, "little")
        # S bit of data-processing and multiply encodings (capstone's update_flags is wrong for sbc)
        self.s = bool(self.word >> 20 & 1)

    def unsupported(self, why=""):
        raise Unsupported(self.ci.mnemonic, self.ci.op_str, why)

    def regnum(self, reg):
        return REG_NUM[self.ci.reg_name(reg)]

    def read(self, reg):
        """C expression for a register operand (PC reads as the address of this instruction + 8)."""
        n = self.regnum(reg)
        return u32(self.lc.base + self.ci.address + 8) if n == 15 else f"c->r[{n}]"

    def dest(self, reg):
        n = self.regnum(reg)
        if n == 15:
            self.unsupported("writes pc")
        return f"c->r[{n}]"

    def op2(self, op, want_carry):
        """(statements, value expression, carry expression or None) for a flexible second operand.

        With want_carry the carry expression is the shifter carry out (C unchanged when there is none)."""
        if op.type == arm.ARM_OP_IMM:
            value = op.imm & 0xFFFFFFFF
            carry = None
            if want_carry and (self.word >> 8) & 0xF:  # a rotated immediate sets C to its bit 31
                carry = str(value >> 31)
            return [], u32(value), carry
        if op.type != arm.ARM_OP_REG:
            self.unsupported("operand kind")
        v = self.read(op.reg)
        st = op.shift.type
        if st == arm.ARM_SFT_INVALID:
            return [], v, None
        kind = SHIFT_KIND[st]
        if st in REG_SHIFTS:
            amount = f"(c->r[{REG_NUM[self.ci.reg_name(op.shift.value)]}] & 0xffu)"
        else:
            amount = str(op.shift.value)
        if not want_carry:
            return [], f"nj_shift(c, {kind}, {v}, {amount})", None
        return ["uint32_t cy = c->c;", f"uint32_t sh = nj_shift_c({kind}, {v}, {amount}, &cy);"], "sh", "cy"


def _three(ins):
    """(rd, rn, op2) for `op rd, rn, op2`, or `op rd, op2` meaning rd = rd op op2."""
    ops = ins.ci.operands
    if len(ops) == 3:
        return ops[0], ops[1], ops[2]
    if len(ops) == 2:
        return ops[0], ops[0], ops[1]
    ins.unsupported("operand count")


def _logic_result(ins, rd, expr, carry):
    """Assign a logical result (rd may be None for tst/teq) and set N, Z, C when the instruction has S."""
    out = [f"uint32_t res = {expr};"]
    if rd is not None:
        out.append(f"{ins.dest(rd.reg)} = res;")
    if ins.s:
        out.append(f"nj_logic_flags(c, res, {carry if carry is not None else 'c->c'});")
    return out


def lift_mov(ins):
    ops = ins.ci.operands
    if len(ops) != 2:
        ins.unsupported("operand count")
    pre, v, carry = ins.op2(ops[1], want_carry=ins.s)
    if ins.ci.id == arm.ARM_INS_MVN:
        v = f"~({v})"
    return pre + _logic_result(ins, ops[0], v, carry)


LOGIC_OP = {arm.ARM_INS_AND: "{a} & {b}", arm.ARM_INS_ORR: "{a} | {b}", arm.ARM_INS_EOR: "{a} ^ {b}",
            arm.ARM_INS_BIC: "{a} & ~({b})", arm.ARM_INS_TST: "{a} & {b}", arm.ARM_INS_TEQ: "{a} ^ {b}"}


def lift_logic(ins):
    if ins.ci.id in (arm.ARM_INS_TST, arm.ARM_INS_TEQ):
        rn, op = ins.ci.operands
        rd = None
    else:
        rd, rn, op = _three(ins)
    pre, v, carry = ins.op2(op, want_carry=ins.s)
    return pre + _logic_result(ins, rd, LOGIC_OP[ins.ci.id].format(a=ins.read(rn.reg), b=v), carry)


# (flag-setting helper and operand order, plain expression) for arithmetic
ARITH_OP = {arm.ARM_INS_ADD: ("nj_adds(c, {a}, {b})", "{a} + {b}"),
            arm.ARM_INS_SUB: ("nj_subs(c, {a}, {b})", "{a} - {b}"),
            arm.ARM_INS_RSB: ("nj_subs(c, {b}, {a})", "{b} - {a}"),
            arm.ARM_INS_ADC: ("nj_adcs(c, {a}, {b})", "{a} + {b} + c->c"),
            arm.ARM_INS_SBC: ("nj_sbcs(c, {a}, {b})", "{a} - {b} - !c->c"),
            arm.ARM_INS_RSC: ("nj_sbcs(c, {b}, {a})", "{b} - {a} - !c->c"),
            arm.ARM_INS_CMP: ("nj_subs(c, {a}, {b})", None),
            arm.ARM_INS_CMN: ("nj_adds(c, {a}, {b})", None)}


def lift_arith(ins):
    flags_fmt, plain_fmt = ARITH_OP[ins.ci.id]
    if plain_fmt is None:  # cmp, cmn
        rn, op = ins.ci.operands
        pre, v, _ = ins.op2(op, want_carry=False)
        return pre + [flags_fmt.format(a=ins.read(rn.reg), b=v) + ";"]
    rd, rn, op = _three(ins)
    pre, v, _ = ins.op2(op, want_carry=False)
    fmt = flags_fmt if ins.s else plain_fmt
    return pre + [f"{ins.dest(rd.reg)} = {fmt.format(a=ins.read(rn.reg), b=v)};"]


SHIFT_ID = {arm.ARM_INS_LSL: "NJ_LSL", arm.ARM_INS_LSR: "NJ_LSR", arm.ARM_INS_ASR: "NJ_ASR",
            arm.ARM_INS_ROR: "NJ_ROR"}


def lift_shift(ins):
    """lsl/lsr/asr/ror: `op rd, rm, #imm` arrives as mov rd, <shifted rm>; `op rd, rm, rs` as three registers."""
    ops = ins.ci.operands
    if len(ops) == 2:
        return lift_mov(ins)
    rd, rm, rs = ops
    amount = f"({ins.read(rs.reg)} & 0xffu)"
    kind = SHIFT_ID[ins.ci.id]
    if not ins.s:
        return [f"{ins.dest(rd.reg)} = nj_shift(c, {kind}, {ins.read(rm.reg)}, {amount});"]
    return ["uint32_t cy = c->c;", f"uint32_t res = nj_shift_c({kind}, {ins.read(rm.reg)}, {amount}, &cy);",
            f"{ins.dest(rd.reg)} = res;", "nj_logic_flags(c, res, cy);"]


def lift_rrx(ins):
    rd, rm = ins.ci.operands
    out = ["uint32_t cy = c->c;", f"uint32_t res = nj_shift_c(NJ_RRX, {ins.read(rm.reg)}, 1, &cy);",
           f"{ins.dest(rd.reg)} = res;"]
    return out + (["nj_logic_flags(c, res, cy);"] if ins.s else [])


def lift_add(ins):
    return lift_arith(ins)


def lift_movt(ins):
    rd, imm = ins.ci.operands
    d = ins.dest(rd.reg)
    return [f"{d} = ({d} & 0xffffu) | {u32((imm.imm & 0xFFFF) << 16)};"]


def _nz(ins, res):
    return [f"c->n = {res} >> 31;", f"c->z = {res} == 0;"] if ins.s else []


def lift_mul(ins):
    ops = ins.ci.operands
    rd, rn, rm = ops[0], ops[1], ops[2]
    prod = f"{ins.read(rn.reg)} * {ins.read(rm.reg)}"
    if ins.ci.id == arm.ARM_INS_MLA:
        prod = f"{prod} + {ins.read(ops[3].reg)}"
    elif ins.ci.id == arm.ARM_INS_MLS:
        prod = f"{ins.read(ops[3].reg)} - {prod}"
    return [f"uint32_t res = {prod};", f"{ins.dest(rd.reg)} = res;"] + _nz(ins, "res")


def lift_mull(ins):
    """umull/smull/umlal/smlal RdLo, RdHi, Rn, Rm."""
    lo, hi, rn, rm = ins.ci.operands
    signed = ins.ci.id in (arm.ARM_INS_SMULL, arm.ARM_INS_SMLAL)
    t = "int64_t" if signed else "uint64_t"
    cast = "(int32_t)" if signed else ""
    out = [f"uint64_t res = (uint64_t)(({t}){cast}{ins.read(rn.reg)} * ({t}){cast}{ins.read(rm.reg)});"]
    if ins.ci.id in (arm.ARM_INS_UMLAL, arm.ARM_INS_SMLAL):
        out.append(f"res += (uint64_t){ins.read(hi.reg)} << 32 | {ins.read(lo.reg)};")
    out += [f"{ins.dest(lo.reg)} = (uint32_t)res;", f"{ins.dest(hi.reg)} = (uint32_t)(res >> 32);"]
    if ins.s:
        out += ["c->n = (uint32_t)(res >> 63);", "c->z = res == 0;"]
    return out


HALF = {"bb": ("", ""), "bt": ("", " >> 16"), "tb": (" >> 16", ""), "tt": (" >> 16", " >> 16")}


def lift_smulxy(ins):
    """smulbb..smultt and smlabb..smlatt: 16x16 signed multiply (+ accumulate, setting Q on overflow)."""
    name = ins.ci.insn_name()
    xy = name[-2:]
    ops = ins.ci.operands
    a = f"(int16_t)({ins.read(ops[1].reg)}{HALF[xy][0]})"
    b = f"(int16_t)({ins.read(ops[2].reg)}{HALF[xy][1]})"
    if name.startswith("smul"):
        return [f"{ins.dest(ops[0].reg)} = (uint32_t)((int32_t){a} * (int32_t){b});"]
    return [f"int64_t res = (int64_t)((int32_t){a} * (int32_t){b}) + (int32_t){ins.read(ops[3].reg)};",
            "if (res != (int32_t)res) c->q = 1;", f"{ins.dest(ops[0].reg)} = (uint32_t)res;"]


def lift_clz(ins):
    rd, rm = ins.ci.operands
    return [f"uint32_t v = {ins.read(rm.reg)};", f"{ins.dest(rd.reg)} = v ? (uint32_t)__builtin_clz(v) : 32u;"]


EXTEND = {arm.ARM_INS_UXTB: ("uint8_t", False), arm.ARM_INS_UXTH: ("uint16_t", False),
          arm.ARM_INS_SXTB: ("int8_t", False), arm.ARM_INS_SXTH: ("int16_t", False),
          arm.ARM_INS_UXTAB: ("uint8_t", True), arm.ARM_INS_UXTAH: ("uint16_t", True),
          arm.ARM_INS_SXTAB: ("int8_t", True), arm.ARM_INS_SXTAH: ("int16_t", True)}


def lift_extend(ins):
    t, acc = EXTEND[ins.ci.id]
    ops = ins.ci.operands
    rm = ops[-1]
    v = ins.read(rm.reg)
    if rm.shift.type == arm.ARM_SFT_ROR and rm.shift.value:
        r = rm.shift.value
        v = f"(({v} >> {r}) | ({v} << {32 - r}))"
    ext = f"(uint32_t)({t})({v})" if t.startswith("u") else f"(uint32_t)(int32_t)({t})({v})"
    if acc:
        ext = f"{ins.read(ops[1].reg)} + {ext}"
    return [f"{ins.dest(ops[0].reg)} = {ext};"]


def _field_mask(lsb, width):
    return ((1 << width) - 1) << lsb


def lift_bitfield(ins):
    ops = ins.ci.operands
    iid = ins.ci.id
    if iid == arm.ARM_INS_BFC:
        rd, lsb, width = ops
        return [f"{ins.dest(rd.reg)} &= {u32(~_field_mask(lsb.imm, width.imm))};"]
    rd, rn, lsb, width = ops
    lsb, width = lsb.imm, width.imm
    v = ins.read(rn.reg)
    if iid == arm.ARM_INS_BFI:
        m = _field_mask(lsb, width)
        d = ins.dest(rd.reg)
        return [f"{d} = ({d} & {u32(~m)}) | (({v} << {lsb}) & {u32(m)});"]
    if iid == arm.ARM_INS_UBFX:
        return [f"{ins.dest(rd.reg)} = ({v} >> {lsb}) & {u32((1 << width) - 1)};"]
    # sbfx: move the field to the top, then arithmetic shift down
    return [f"{ins.dest(rd.reg)} = (uint32_t)((int32_t)({v} << {32 - lsb - width}) >> {32 - width});"]


# ---- loads and stores (addressing decoded from the encoding; capstone's operand details are inconsistent) ----

SHIFT_TYPE = ("NJ_LSL", "NJ_LSR", "NJ_ASR", "NJ_ROR")


def _reg(ins, n):
    return u32(ins.lc.base + ins.ci.address + 8) if n == 15 else f"c->r[{n}]"


def _writeback(ins, n):
    if n == 15:
        ins.unsupported("writeback to pc")
    return f"c->r[{n}]"


def _index_and_writeback(ins, off):
    """Address and write-back statements for P/U/W addressing with offset expression `off`."""
    w = ins.word
    p, up, wb = w >> 24 & 1, w >> 23 & 1, w >> 21 & 1
    n = w >> 16 & 15
    sign = "+" if up else "-"
    out = [f"uint32_t base = {_reg(ins, n)};", f"uint32_t off = {off};",
           f"uint32_t addr = {'base ' + sign + ' off' if p else 'base'};"]
    upd = [f"{_writeback(ins, n)} = base {sign} off;"] if (wb or not p) else []
    return out, upd


def lift_single(ins):
    """ldr/str/ldrb/strb (and the user-mode t variants, which behave the same here)."""
    w = ins.word
    t = w >> 12 & 15
    if w >> 25 & 1:  # register offset with immediate shift
        imm5, typ = w >> 7 & 31, w >> 5 & 3
        rm = _reg(ins, w & 15)
        if typ == 3 and imm5 == 0:
            off = f"nj_shift(c, NJ_RRX, {rm}, 1)"
        else:
            amount = 32 if imm5 == 0 and typ in (1, 2) else imm5
            off = rm if amount == 0 else f"nj_shift(c, {SHIFT_TYPE[typ]}, {rm}, {amount})"
    else:
        off = u32(w & 0xFFF)
    out, upd = _index_and_writeback(ins, off)
    byte, load = w >> 22 & 1, w >> 20 & 1
    if load:
        if t == 15:
            ins.unsupported("writes pc")
        out.append(f"uint32_t v = {'nj_r8(addr)' if byte else 'nj_r32(addr)'};")
        return out + upd + [f"c->r[{t}] = v;"]
    v = _reg(ins, t)
    out.append(f"nj_w8(addr, (uint8_t){v});" if byte else f"nj_w32(addr, {v});")
    return out + upd


def lift_extra(ins):
    """ldrh/strh/ldrsb/ldrsh/ldrd/strd."""
    w = ins.word
    t = w >> 12 & 15
    off = u32((w >> 4 & 0xF0) | (w & 0xF)) if w >> 22 & 1 else _reg(ins, w & 15)
    out, upd = _index_and_writeback(ins, off)
    op2, load = w >> 5 & 3, w >> 20 & 1
    if load and op2 == 1:
        kind = "ldrh"
    elif load:
        kind = "ldrsb" if op2 == 2 else "ldrsh"
    else:
        kind = "strh" if op2 == 1 else ("ldrd" if op2 == 2 else "strd")
    if kind in ("ldrd", "strd") and (t & 1 or t == 14):
        ins.unsupported("odd register pair")
    if t == 15 or (kind == "ldrd" and t + 1 == 15):
        ins.unsupported("writes pc")
    if kind == "ldrh":
        return out + ["uint32_t v = nj_r16(addr);"] + upd + [f"c->r[{t}] = v;"]
    if kind == "ldrsb":
        return out + ["uint32_t v = (uint32_t)(int32_t)(int8_t)nj_r8(addr);"] + upd + [f"c->r[{t}] = v;"]
    if kind == "ldrsh":
        return out + ["uint32_t v = (uint32_t)(int32_t)(int16_t)nj_r16(addr);"] + upd + [f"c->r[{t}] = v;"]
    if kind == "strh":
        return out + [f"nj_w16(addr, (uint16_t){_reg(ins, t)});"] + upd
    if kind == "ldrd":
        return out + ["uint32_t v0 = nj_r32(addr), v1 = nj_r32(addr + 4);"] + upd + [
            f"c->r[{t}] = v0;", f"c->r[{t + 1}] = v1;"]
    return out + [f"nj_w32(addr, {_reg(ins, t)});", f"nj_w32(addr + 4, {_reg(ins, t + 1)});"] + upd


def lift_block(ins):
    """ldm/stm in all four modes (push/pop included); registers in ascending order at ascending addresses."""
    w = ins.word
    p, up, s_bit, wb, load = w >> 24 & 1, w >> 23 & 1, w >> 22 & 1, w >> 21 & 1, w >> 20 & 1
    n = w >> 16 & 15
    regs = [r for r in range(16) if w >> r & 1]
    if s_bit or not regs or n == 15:
        ins.unsupported("user-bank or empty transfer")
    if load and 15 in regs:
        ins.unsupported("writes pc")
    count = len(regs)
    first = {(0, 1): 0, (1, 1): 4, (0, 0): 4 - 4 * count, (1, 0): -4 * count}[(p, up)]
    out = [f"uint32_t base = c->r[{n}];"]
    if load:
        out += [f"uint32_t v{i} = nj_r32(base + {u32(first + 4 * i)});" for i in range(count)]
        if wb and n not in regs:  # with the base in the list the loaded value wins
            out.append(f"c->r[{n}] = base {'+' if up else '-'} {4 * count}u;")
        out += [f"c->r[{r}] = v{i};" for i, r in enumerate(regs)]
        return out
    for i, r in enumerate(regs):
        v = "base" if r == n else _reg(ins, r)
        out.append(f"nj_w32(base + {u32(first + 4 * i)}, {v});")
    if wb:
        out.append(f"c->r[{n}] = base {'+' if up else '-'} {4 * count}u;")
    return out


def lift_mem(ins):
    w = ins.word
    if w >> 26 & 3 == 1:
        return lift_single(ins)
    if w >> 25 & 7 == 4:
        return lift_block(ins)
    if w >> 25 & 7 == 0 and w >> 7 & 1 and w >> 4 & 1 and w >> 5 & 3:
        return lift_extra(ins)
    ins.unsupported("memory encoding")


MEM_IDS = ("LDR", "LDRB", "LDRT", "LDRBT", "STR", "STRB", "STRT", "STRBT", "LDRH", "STRH", "LDRSB", "LDRSH",
           "LDRD", "STRD", "LDM", "LDMIB", "LDMDA", "LDMDB", "STM", "STMIB", "STMDA", "STMDB", "PUSH", "POP")


HANDLERS = {
    arm.ARM_INS_MOV: lift_mov, arm.ARM_INS_MVN: lift_mov,
    arm.ARM_INS_AND: lift_logic, arm.ARM_INS_ORR: lift_logic, arm.ARM_INS_EOR: lift_logic,
    arm.ARM_INS_BIC: lift_logic, arm.ARM_INS_TST: lift_logic, arm.ARM_INS_TEQ: lift_logic,
    arm.ARM_INS_ADD: lift_arith, arm.ARM_INS_SUB: lift_arith, arm.ARM_INS_RSB: lift_arith,
    arm.ARM_INS_ADC: lift_arith, arm.ARM_INS_SBC: lift_arith, arm.ARM_INS_RSC: lift_arith,
    arm.ARM_INS_CMP: lift_arith, arm.ARM_INS_CMN: lift_arith,
    arm.ARM_INS_LSL: lift_shift, arm.ARM_INS_LSR: lift_shift, arm.ARM_INS_ASR: lift_shift,
    arm.ARM_INS_ROR: lift_shift, arm.ARM_INS_RRX: lift_rrx,
    arm.ARM_INS_MOVT: lift_movt,
    arm.ARM_INS_MUL: lift_mul, arm.ARM_INS_MLA: lift_mul, arm.ARM_INS_MLS: lift_mul,
    arm.ARM_INS_UMULL: lift_mull, arm.ARM_INS_SMULL: lift_mull, arm.ARM_INS_UMLAL: lift_mull,
    arm.ARM_INS_SMLAL: lift_mull,
    arm.ARM_INS_SMULBB: lift_smulxy, arm.ARM_INS_SMULBT: lift_smulxy, arm.ARM_INS_SMULTB: lift_smulxy,
    arm.ARM_INS_SMULTT: lift_smulxy, arm.ARM_INS_SMLABB: lift_smulxy, arm.ARM_INS_SMLABT: lift_smulxy,
    arm.ARM_INS_SMLATB: lift_smulxy, arm.ARM_INS_SMLATT: lift_smulxy,
    arm.ARM_INS_CLZ: lift_clz,
    arm.ARM_INS_UXTB: lift_extend, arm.ARM_INS_UXTH: lift_extend, arm.ARM_INS_SXTB: lift_extend,
    arm.ARM_INS_SXTH: lift_extend, arm.ARM_INS_UXTAB: lift_extend, arm.ARM_INS_UXTAH: lift_extend,
    arm.ARM_INS_SXTAB: lift_extend, arm.ARM_INS_SXTAH: lift_extend,
    arm.ARM_INS_BFI: lift_bitfield, arm.ARM_INS_BFC: lift_bitfield, arm.ARM_INS_UBFX: lift_bitfield,
    arm.ARM_INS_SBFX: lift_bitfield,
} | {getattr(arm, "ARM_INS_" + n): lift_mem for n in MEM_IDS if hasattr(arm, "ARM_INS_" + n)}


def lift_insn(ci, lc):
    """C statements for one capstone ARM instruction (detail mode)."""
    handler = HANDLERS.get(ci.id)
    if handler is None:
        raise Unsupported(ci.mnemonic, ci.op_str)
    body = ["    " + s for s in handler(_Insn(ci, lc))]
    head = f"if (NJ_COND_{COND[ci.cc]}(c)) {{" if ci.cc in COND else "{"
    return [head] + body + ["}"]
