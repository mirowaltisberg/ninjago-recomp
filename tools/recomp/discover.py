"""Function discovery for ARM-mode guest code by recursive descent.

Entries: exported functions, .ARM.exidx starts, relocated pointers into .text and every BL target.
Each function body is the code reachable from its entry; branches to other entries are tail calls.
Literal-pool words (targets of PC-relative loads) are never decoded as code. Thumb entries (BLX
targets, or entries whose first words do not look like ARM code) are listed, not decoded.
"""
from dataclasses import dataclass, field

import capstone
from capstone import arm

from .elfimage import BytesImage

NORETURN_IMPORTS = {"abort", "__stack_chk_fail", "__assert2", "exit", "_exit", "__cxa_throw", "__cxa_rethrow"}
PCREL_LOADS = {arm.ARM_INS_LDR: 4, arm.ARM_INS_LDRB: 4, arm.ARM_INS_LDRH: 4, arm.ARM_INS_LDRSB: 4,
               arm.ARM_INS_LDRSH: 4, arm.ARM_INS_LDRD: 8, arm.ARM_INS_VLDR: None}


@dataclass(slots=True)
class Insn:
    addr: int
    word: int
    id: int
    mnemonic: str
    op_str: str
    cc: int


@dataclass
class JumpTable:
    at: int
    index_reg: str
    count: int
    targets: list
    default: int
    kind: str  # "add" (table of branches) or "ldr" (table of addresses)


@dataclass
class Function:
    entry: int
    insns: dict = field(default_factory=dict)
    labels: set = field(default_factory=set)
    jump_tables: dict = field(default_factory=dict)
    callees: set = field(default_factory=set)
    tail_targets: set = field(default_factory=set)


@dataclass
class Program:
    functions: dict = field(default_factory=dict)
    thumb_functions: dict = field(default_factory=dict)
    issues: list = field(default_factory=list)


class _Decoder:
    def __init__(self, image):
        self.image = image
        self.md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        self.md.detail = True
        self.cache = {}

    def full(self, addr):
        """Capstone instruction with details (not cached)."""
        try:
            raw = self.image.read_bytes(addr, 4)
        except ValueError:
            return None
        return next(self.md.disasm(raw, addr), None)

    def __call__(self, addr):
        if addr in self.cache:
            return self.cache[addr]
        ci = self.full(addr)
        ins = None
        if ci is not None:
            ins = Insn(addr, int.from_bytes(ci.bytes, "little"), ci.id, ci.mnemonic, ci.op_str, ci.cc)
        self.cache[addr] = (ins, ci) if ci is not None else (None, None)
        return self.cache[addr]


def _looks_arm(image, entry, next_entry):
    """ARM code is dominated by the AL condition (top nibble 0xE); Thumb bytes read as ARM are not."""
    n = max(1, min(12, (next_entry - entry) // 4))
    words = []
    for i in range(n):
        try:
            words.append(image.read32(entry + 4 * i))
        except ValueError:
            break
    if not words:
        return False
    return sum(1 for w in words if (w >> 28) == 0xE) / len(words) >= 0.5


def _pcrel_targets(ci):
    if ci.id not in PCREL_LOADS or len(ci.operands) < 2:
        return []
    op = ci.operands[-1]
    if op.type != arm.ARM_OP_MEM or op.mem.base != arm.ARM_REG_PC or op.mem.index != 0:
        return []
    disp = op.mem.disp
    if getattr(op, "subtracted", False) and disp > 0:
        disp = -disp
    t = ((ci.address + 8) & ~3) + disp
    size = PCREL_LOADS[ci.id]
    if size is None:  # vldr: .32 or .64
        size = 8 if "d" in ci.op_str.split(",")[0] else 4
    return [t] if size == 4 else [t, t + 4]


class _Discoverer:
    def __init__(self, image):
        self.image = image
        self.dec = _Decoder(image)
        self.lo, self.hi = image.text
        self.prog = Program()
        self.pool = set()
        self.entries = set()
        self.thumb_block = (0, 0)
        self.named_arm = set()  # even-valued function symbols: ARM by the ELF convention

    def in_text(self, a):
        return self.lo <= a < self.hi

    # -- one function --------------------------------------------------------------------------
    def descend(self, entry, collect_pool, new_entries):
        f = Function(entry)
        work = [entry]
        issues = []
        while work:
            a = work.pop()
            while True:
                if a in f.insns:
                    break
                if a != entry and a in self.entries:
                    f.tail_targets.add(a)  # falls through into another function
                    break
                if not self.in_text(a):
                    issues.append(f"unresolved: path leaves .text at {a:#x} in {entry:#x}")
                    break
                if a in self.pool:
                    break  # e.g. after a call to a no-return function
                ins, ci = self.dec(a)
                if ins is None:
                    issues.append(f"unresolved: undecodable word at {a:#x} in {entry:#x}")
                    break
                f.insns[a] = ins
                if collect_pool:
                    self.pool.update(_pcrel_targets(ci))
                cont = self.step(f, ci, work, new_entries, issues)
                if not cont:
                    break
                a += 4
        return f, issues

    def branch_to(self, f, target, work):
        if target in self.image.plt or (target != f.entry and target in self.entries):
            f.tail_targets.add(target)
        else:
            f.labels.add(target)
            work.append(target)

    def step(self, f, ci, work, new_entries, issues):
        """Record control flow of one instruction; return True if execution can fall through."""
        cond = ci.cc not in (arm.ARM_CC_AL, arm.ARM_CC_INVALID)
        iid = ci.id
        ops = ci.operands
        if iid == arm.ARM_INS_B:
            self.branch_to(f, ops[0].imm, work)
            return cond
        if iid == arm.ARM_INS_BL:
            t = ops[0].imm
            f.callees.add(t)
            if t in self.image.plt:
                return not (self.image.plt[t] in NORETURN_IMPORTS and not cond)
            if self.in_text(t) and t not in self.entries:
                new_entries.add(t)
            return True
        if iid == arm.ARM_INS_BLX:
            if ops[0].type == arm.ARM_OP_IMM:
                t = ops[0].imm
                f.callees.add(t)
                self.prog.thumb_functions.setdefault(t & ~1, None)
            return True
        if iid == arm.ARM_INS_BX:
            return cond  # return (lr) or indirect tail jump
        writes = ci.regs_access()[1]
        if arm.ARM_REG_PC not in writes:
            return True
        # other PC writes
        if iid in (arm.ARM_INS_POP, arm.ARM_INS_LDM, arm.ARM_INS_LDMIB, arm.ARM_INS_LDMDA, arm.ARM_INS_LDMDB):
            return cond
        if iid == arm.ARM_INS_MOV and len(ops) == 2 and ops[1].type == arm.ARM_OP_REG:
            return cond  # mov pc, lr (return) / mov pc, rX (indirect jump)
        if iid == arm.ARM_INS_LDR and len(ops) == 2 and ops[1].type == arm.ARM_OP_MEM:
            m = ops[1].mem
            if m.base == arm.ARM_REG_PC and m.index != 0:
                return self.ldr_table(f, ci, work, issues, cond)
            if m.base == arm.ARM_REG_SP:
                return cond  # ldr pc, [sp], #4: return
        if iid == arm.ARM_INS_ADD and len(ops) == 3 and ops[1].type == arm.ARM_OP_REG and ops[1].reg == arm.ARM_REG_PC \
                and ops[2].type == arm.ARM_OP_REG and ops[2].shift.type == arm.ARM_SFT_LSL and ops[2].shift.value == 2:
            return self.add_table(f, ci, work, issues)
        issues.append(f"unresolved: pc write {ci.mnemonic} {ci.op_str} at {ci.address:#x} in {f.entry:#x}")
        return False

    def bound(self, f, ci, reg):
        """Entry count from the compare guarding a switch: cmp reg, #imm (ls: imm+1 cases, lo: imm)."""
        prev, pci = self.dec(ci.address - 4)
        if pci is None or pci.id != arm.ARM_INS_CMP or len(pci.operands) != 2:
            return None
        if pci.operands[0].reg != reg or pci.operands[1].type != arm.ARM_OP_IMM:
            return None
        imm = pci.operands[1].imm
        return imm + 1 if ci.cc == arm.ARM_CC_LS else imm if ci.cc == arm.ARM_CC_LO else None

    def add_table(self, f, ci, work, issues):
        reg = ci.operands[2].reg
        count = self.bound(f, ci, reg)
        if count is None:
            issues.append(f"unresolved: switch without bound at {ci.address:#x} in {f.entry:#x}")
            return False
        targets = [ci.address + 8 + 4 * i for i in range(count)]
        jt = JumpTable(ci.address, ci.reg_name(reg), count, targets, ci.address + 4, "add")
        f.jump_tables[ci.address] = jt
        for t in targets:
            f.labels.add(t)
            work.append(t)
        f.labels.add(jt.default)
        return True  # default case falls through to the next instruction

    def ldr_table(self, f, ci, work, issues, cond):
        reg = ci.operands[1].mem.index
        count = self.bound(f, ci, reg)
        if count is None:
            issues.append(f"unresolved: address table without bound at {ci.address:#x} in {f.entry:#x}")
            return False
        table = ci.address + 8
        targets = []
        for i in range(count):
            self.pool.add(table + 4 * i)
            targets.append(self.image.read32(table + 4 * i) & ~1)
        f.jump_tables[ci.address] = JumpTable(ci.address, ci.reg_name(reg), count, targets, ci.address + 4, "ldr")
        for t in targets:
            self.branch_to(f, t, work)
        return cond

    # -- whole program ---------------------------------------------------------------------------
    def classify(self, seeds):
        """Split entries into ARM entries and Thumb functions. ARM is the default; Thumb needs evidence:
        an odd address (symbol or relocated pointer), a BLX-immediate target, or, only inside the
        statically linked Thumb runtime block, first words that do not look like ARM code. An even-valued
        function symbol is ARM regardless (libgcc's ARM helpers live inside that block)."""
        ordered = sorted(seeds)
        arm_entries = set()
        for i, e in enumerate(ordered):
            if not self.in_text(e):
                continue
            nxt = ordered[i + 1] if i + 1 < len(ordered) else self.hi
            in_block = self.thumb_block[0] <= e < self.thumb_block[1]
            guessed_thumb = in_block and e not in self.named_arm and not _looks_arm(self.image, e, nxt)
            if e in self.prog.thumb_functions or guessed_thumb:
                self.prog.thumb_functions.setdefault(e, None)
            else:
                arm_entries.add(e)
        return arm_entries

    def run_pass(self, collect_pool):
        functions, issues = {}, []
        pending = sorted(self.entries)
        while pending:
            new_entries = set()
            for e in pending:
                if e in functions:
                    continue
                f, iss = self.descend(e, collect_pool, new_entries)
                functions[e] = f
                issues.extend(iss)
            new_entries -= self.entries
            new_entries -= set(self.prog.thumb_functions)
            fresh = self.classify(new_entries)
            self.entries |= fresh
            pending = sorted(fresh)
        return functions, issues

    def run(self, seeds):
        odd = sorted(s & ~1 for s in seeds if s & 1)
        for t in odd:
            self.prog.thumb_functions.setdefault(t, None)
        # the Thumb runtime block: the cluster of odd entries near the end of .text, plus a margin
        tail = [t for t in odd if t >= self.hi - 0x20000]
        self.thumb_block = (tail[0] - 0x1000, tail[-1] + 0x1000) if tail else (0, 0)
        self.entries = self.classify({s & ~1 for s in seeds})
        self.run_pass(collect_pool=True)    # pass 1: learn literal pools (and BL targets)
        functions, issues = self.run_pass(collect_pool=False)  # pass 2: bodies that respect the pools
        for t in list(self.prog.thumb_functions):
            self.entries.discard(t)
            functions.pop(t, None)
            name, off = self.image.nearest_symbol(t)
            self.prog.thumb_functions[t] = name if off == 0 else (f"{name}+{off:#x}" if name else None)
        self.prog.functions = functions
        self.prog.issues = issues
        return self.prog


def discover(elf):
    seeds = set()
    for name, v in elf.exported.items():
        if elf.text[0] <= (v & ~1) < elf.text[1]:
            seeds.add(v)
    seeds |= {e for e in elf.exidx_starts if elf.text[0] <= e < elf.text[1]}
    seeds |= elf.reloc_ptrs
    d = _Discoverer(elf)
    d.named_arm = {v for v in elf.exported.values() if not v & 1 and elf.text[0] <= v < elf.text[1]}
    return d.run(seeds)


def discover_bytes(code, base_vaddr, entries):
    return _Discoverer(BytesImage(code, base_vaddr)).run(set(entries))
