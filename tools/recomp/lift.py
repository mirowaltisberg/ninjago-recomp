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


def lift_mov(ins):
    ops = ins.ci.operands
    if len(ops) != 2:
        ins.unsupported("operand count")
    flags = ins.ci.update_flags
    pre, v, carry = ins.op2(ops[1], want_carry=flags)
    if not flags:
        return pre + [f"{ins.dest(ops[0].reg)} = {v};"]
    return pre + [f"uint32_t res = {v};", f"{ins.dest(ops[0].reg)} = res;",
                  f"nj_logic_flags(c, res, {carry if carry is not None else 'c->c'});"]


def lift_add(ins):
    rd, rn, op = _three(ins)
    pre, v, _ = ins.op2(op, want_carry=False)
    a = ins.read(rn.reg)
    if ins.ci.update_flags:
        return pre + [f"{ins.dest(rd.reg)} = nj_adds(c, {a}, {v});"]
    return pre + [f"{ins.dest(rd.reg)} = {a} + {v};"]


HANDLERS = {
    arm.ARM_INS_MOV: lift_mov,
    arm.ARM_INS_ADD: lift_add,
}


def lift_insn(ci, lc):
    """C statements for one capstone ARM instruction (detail mode)."""
    handler = HANDLERS.get(ci.id)
    if handler is None:
        raise Unsupported(ci.mnemonic, ci.op_str)
    body = ["    " + s for s in handler(_Insn(ci, lc))]
    head = f"if (NJ_COND_{COND[ci.cc]}(c)) {{" if ci.cc in COND else "{"
    return [head] + body + ["}"]
