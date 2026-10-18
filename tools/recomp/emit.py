"""Emission of whole functions and the dispatch table (see cli.py).

Each discovered ARM function becomes `void f_<link addr>(NjCtx *restrict c)`. Control flow:
  b intra-function       -> goto L_<addr>
  b elsewhere            -> tail call (function, import or indirect), then return
  bl / blx imm           -> lr = return address, then call (function, import, or indirect for Thumb/overrides)
  blx reg / bx reg       -> nj_aot_call_indirect (bx: as a tail call); bx lr and loads into pc return
  add pc, pc, rX, lsl #2 -> switch on rX with goto per case
Returns go through NJ_RET, which in checked builds verifies the target is the lr the function was entered with.
Instructions are emitted in address order; a function whose entry is not its lowest address starts with a goto.
A function containing an instruction the lifter cannot translate becomes a stub that fails loudly.
"""
import os
import re

import capstone
from capstone import arm

from .discover import NORETURN_IMPORTS
from .lift import REG_NUM, Unsupported, cond_head, lift_body, u32


def fname(addr):
    return f"f_{addr:08x}"


def _ident(name):
    return "imp_" + re.sub(r"[^A-Za-z0-9_]", "_", name)


class _FunctionEmitter:
    def __init__(self, fn, prog, elf, lc, md, imports):
        self.fn = fn
        self.prog = prog
        self.elf = elf
        self.lc = lc
        self.md = md
        self.imports = imports  # import name -> C identifier, shared per output file
        self.base = lc.base
        self.labels = {t for t in fn.labels if t in fn.insns}
        for jt in fn.jump_tables.values():
            self.labels |= {t for t in jt.targets if t in fn.insns}
        # code at lower addresses than the entry (shared blocks reached by branching back) comes first in the
        # C text, so execution must jump to the entry
        self.entry_first = min(fn.insns) == fn.entry
        if not self.entry_first:
            self.labels.add(fn.entry)

    def import_call(self, name):
        ident = self.imports.setdefault(name, _ident(name))
        return f'NJ_IMPORT({ident}, "{name}");'

    def call(self, target, thumb=False):
        """Statements calling `target` (lr already set)."""
        if target in self.elf.plt:
            return [self.import_call(self.elf.plt[target])]
        guest = self.base + target
        if not thumb and target in self.prog.functions and guest not in self.lc.overrides:
            return [f"{fname(target)}(c);"]
        return [f"nj_aot_call_indirect(c, {u32(guest | (1 if thumb else 0))});"]

    def branch(self, target):
        if target in self.fn.insns:
            return [f"goto L_{target:08x};"]
        return self.call(target) + ["return;"]

    def insn(self, ci):
        """(statements without condition, falls through when executed)."""
        a = ci.address
        iid = ci.id
        ops = ci.operands
        ret = u32(self.base + a + 4)
        entry = u32(self.base + self.fn.entry)
        if iid == arm.ARM_INS_B:
            return self.branch(ops[0].imm), False
        if iid == arm.ARM_INS_BL:
            t = ops[0].imm
            out = [f"c->r[14] = {ret};"] + self.call(t)
            if t in self.elf.plt and self.elf.plt[t] in NORETURN_IMPORTS:
                return out + ["return;"], False
            return out, True
        if iid == arm.ARM_INS_BLX:
            if ops[0].type == arm.ARM_OP_IMM:
                return [f"c->r[14] = {ret};"] + self.call(ops[0].imm, thumb=True), True
            r = REG_NUM[ci.reg_name(ops[0].reg)]
            return [f"uint32_t t = c->r[{r}];", f"c->r[14] = {ret};", "nj_aot_call_indirect(c, t);"], True
        if iid == arm.ARM_INS_BX:
            r = REG_NUM[ci.reg_name(ops[0].reg)]
            if r == 14:
                return [f"NJ_RET(c->r[14], {entry});"], False
            return [f"nj_aot_call_indirect(c, c->r[{r}]);", "return;"], False
        if a in self.fn.jump_tables:
            jt = self.fn.jump_tables[a]
            if jt.kind != "add":
                raise Unsupported(ci.mnemonic, ci.op_str, "address table")
            cases = " ".join(f"case {i}: goto L_{t:08x};" for i, t in enumerate(jt.targets))
            return [f"switch (c->r[{REG_NUM[jt.index_reg]}]) {{ {cases} default: break; }}"], True
        if arm.ARM_REG_PC in ci.regs_access()[1]:
            body = lift_body(ci, self.lc, pc_var="npc")
            return ["uint32_t npc;"] + body + [f"NJ_RET(npc, {entry});"], False
        return lift_body(ci, self.lc), True

    def emit(self):
        out = [f"void {fname(self.fn.entry)}(NjCtx *restrict c) {{",
               "    const uint32_t entry_lr = c->r[14];", "    (void)entry_lr;"]
        if not self.entry_first:
            out.append(f"    goto L_{self.fn.entry:08x};")
        for a in sorted(self.fn.insns):
            ci = next(self.md.disasm(self.elf.read_bytes(a, 4), a))
            if a in self.labels:
                out.append(f"L_{a:08x}: ;")
            body, falls = self.insn(ci)
            conditional = ci.cc not in (arm.ARM_CC_AL, arm.ARM_CC_INVALID)
            out.append(f"    /* {a:#x}: {ci.mnemonic} {ci.op_str} */")
            out.append("    " + cond_head(ci))
            out += ["        " + s for s in body]
            out.append("    }")
            if (falls or conditional) and a + 4 not in self.fn.insns:
                nxt = a + 4  # falls into the next function (a tail call) or into a literal pool (unreachable)
                out += ["    " + s for s in (self.call(nxt) + ["return;"] if nxt in self.prog.functions else ["return;"])]
        out.append("}")
        return "\n".join(out) + "\n"


def emit_function(fn, prog, elf, lc, md, imports):
    """(C text, stub reason or None) for one function."""
    try:
        return _FunctionEmitter(fn, prog, elf, lc, md, imports).emit(), None
    except Unsupported as e:
        why = str(e).replace("\\", "\\\\").replace('"', "'")
        return (f"void {fname(fn.entry)}(NjCtx *restrict c) {{\n    (void)c;\n"
                f"    nj_aot_untranslated({u32(lc.base + fn.entry)}, \"{why}\");\n}}\n"), why


FILE_HEAD = ("/* Generated by tools/recomp/emit.py from the guest library. Do not edit. */\n"
             "#include \"recomp_decls.h\"\n"
             "#pragma clang diagnostic ignored \"-Wunused-label\"\n"
             "/* imports go through their HLE thunk (cached per call site) so tracing sees them like under Unicorn */\n"
             "#define NJ_IMPORT(var, name) hle_dispatch(c, (var) ? (var) : ((var) = hle_thunk(name)))\n\n")


def emit_program(prog, elf, out_dir, lc, funcs_per_file=200, only=None, files=None):
    """With `files`, split into exactly that many f_NNN.c files (so a build system knows the outputs)."""
    os.makedirs(out_dir, exist_ok=True)
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    md.detail = True
    entries = sorted(e for e in prog.functions if only is None or e in only)
    with open(os.path.join(out_dir, "recomp_decls.h"), "w") as h:
        h.write("/* Generated by tools/recomp/emit.py. */\n#pragma once\n#include \"recomp_rt.h\"\n\n")
        h.writelines(f"void {fname(e)}(NjCtx *restrict c);\n" for e in entries)
    if files:
        funcs_per_file = max(1, -(-len(entries) // files))
    stubs = 0
    nfiles = 0
    for start in range(0, max(len(entries), 1), funcs_per_file):
        chunk = entries[start:start + funcs_per_file]
        imports, bodies = {}, []
        for e in chunk:
            text, why = emit_function(prog.functions[e], prog, elf, lc, md, imports)
            stubs += why is not None
            bodies.append(text)
        with open(os.path.join(out_dir, f"f_{nfiles:03d}.c"), "w") as f:
            f.write(FILE_HEAD)
            f.writelines(f"static uint32_t {ident};\n" for ident in sorted(imports.values()))
            f.write("\n")
            f.write("\n".join(bodies))
        nfiles += 1
    while files and nfiles < files:  # pad so the file list is fixed
        with open(os.path.join(out_dir, f"f_{nfiles:03d}.c"), "w") as f:
            f.write(FILE_HEAD)
        nfiles += 1
    with open(os.path.join(out_dir, "dispatch.c"), "w") as d:
        d.write("/* Generated by tools/recomp/emit.py. */\n#include \"recomp_decls.h\"\n\nconst NjAotEntry nj_aot_table[] = {\n")
        d.writelines(f"    {{{u32(lc.base + e)}, {fname(e)}}},\n" for e in entries)
        if not entries:
            d.write("    {0, 0},\n")
        d.write(f"}};\nconst uint32_t nj_aot_count = {len(entries)};\n")
    return {"functions": len(entries), "files": nfiles, "stubs": stubs}
