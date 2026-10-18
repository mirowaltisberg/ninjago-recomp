"""Pick functions for the function-level differential test (tests/funcdiff).

    python -m recomp.funcdiff_pick <so> --out build/funcdiff [--max 300]

A candidate's whole direct-call closure must stay inside translated ARM code: no imports, no Thumb targets,
no indirect calls or jumps and no untranslatable instructions. Math/string/matrix/collision helpers come
first, then other such functions by address. Writes picks.txt (entries to compare) and only.txt (entries to
emit: the picks and their callees).
"""
import argparse
import os
import re

import capstone
from capstone import arm

from .discover import discover
from .elfimage import GuestElf
from .lift import LiftCtx, Unsupported, lift_insn

PREFERRED = re.compile(r"(fnMaths_|fnString|fnaMatrix_|fnStr|geCollision)")  # inside mangled names


def self_contained(prog, elf, md):
    """Entries whose own code is translatable and makes only direct calls to ARM functions."""
    lc = LiftCtx(base=elf.base, plt=elf.plt)
    ok = {}
    for e, f in prog.functions.items():
        good = True
        for a in sorted(f.insns):
            ci = next(md.disasm(elf.read_bytes(a, 4), a))
            if ci.id in (arm.ARM_INS_BLX, arm.ARM_INS_BX) and not (ci.id == arm.ARM_INS_BX and ci.op_str == "lr"):
                good = False
                break
            if ci.id in (arm.ARM_INS_B, arm.ARM_INS_BL, arm.ARM_INS_BX) or arm.ARM_REG_PC in ci.regs_access()[1]:
                continue
            try:
                lift_insn(ci, lc)
            except Unsupported:
                good = False
                break
        targets = f.callees | f.tail_targets
        good = good and all(t in prog.functions for t in targets)
        ok[e] = (good, targets)
    return ok


def closure(e, ok, memo, active=None):
    """Set of entries reachable from e by direct calls, or None if any of them is not self-contained.
    Cycles are allowed (a function on the active path counts as fine until proven otherwise)."""
    if e in memo:
        return memo[e]
    active = active if active is not None else set()
    if e in active:
        return set()
    good, targets = ok[e]
    if not good:
        memo[e] = None
        return None
    active.add(e)
    result = {e}
    for t in targets:
        sub = closure(t, ok, memo, active)
        if sub is None:
            result = None
            break
        result |= sub
    active.discard(e)
    memo[e] = result
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("so")
    ap.add_argument("--out", required=True)
    ap.add_argument("--max", type=int, default=300)
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x100000)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    if not os.path.exists(args.so):
        open(os.path.join(args.out, "picks.txt"), "w").close()
        open(os.path.join(args.out, "only.txt"), "w").close()
        return
    elf = GuestElf.load(args.so, args.base)
    prog = discover(elf)
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    md.detail = True
    ok = self_contained(prog, elf, md)
    memo = {}
    names = {}
    for e in prog.functions:
        n, off = elf.nearest_symbol(e)
        names[e] = n if off == 0 and n else f"sub_{e:x}"
    cands = [e for e in prog.functions if closure(e, ok, memo)]
    cands.sort(key=lambda e: (not PREFERRED.search(names[e]), e))
    picks = cands[:args.max]
    only = set()
    for e in picks:
        only |= closure(e, ok, memo)
    with open(os.path.join(args.out, "picks.txt"), "w") as fh:
        fh.writelines(f"{e:#x} {names[e]}\n" for e in picks)
    with open(os.path.join(args.out, "only.txt"), "w") as fh:
        fh.writelines(f"{e:#x}\n" for e in sorted(only))
    preferred = sum(1 for e in picks if PREFERRED.search(names[e]))
    print(f"funcdiff_pick: {len(cands)} self-contained functions, picked {len(picks)} ({preferred} preferred), "
          f"emitting {len(only)}")


if __name__ == "__main__":
    main()
