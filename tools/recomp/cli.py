"""Recompile the guest library to C.

    python -m recomp.cli <so> --base 0x100000 --out gen [--only ADDR,...] [--only-file F] [--funcs-per-file 200]

Writes gen/recomp_decls.h, gen/f_NNN.c (translated functions) and gen/dispatch.c (guest address -> function).
"""
import argparse

from .discover import discover
from .elfimage import GuestElf
from .emit import emit_program
from .lift import LiftCtx


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("so")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x100000)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="", help="comma-separated link addresses to emit")
    ap.add_argument("--only-file", default="", help="file with one link address per line")
    ap.add_argument("--funcs-per-file", type=int, default=200)
    ap.add_argument("--files", type=int, default=0, help="split into exactly this many files")
    args = ap.parse_args()
    elf = GuestElf.load(args.so, args.base)
    prog = discover(elf)
    only = {int(a, 0) for a in args.only.split(",") if a}
    if args.only_file:
        with open(args.only_file) as fh:
            only |= {int(line.split()[0], 0) for line in fh if line.strip()}
    stats = emit_program(prog, elf, args.out, LiftCtx(base=args.base, plt=elf.plt), args.funcs_per_file,
                         only or None, args.files or None)
    print(f"recomp: {stats['functions']} functions in {stats['files']} files, {stats['stubs']} untranslated stubs")


if __name__ == "__main__":
    main()
