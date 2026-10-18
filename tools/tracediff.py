"""Compare two HLE call traces (harness --trace) thread by thread.

    python -I tools/tracediff.py A B [--context 20]

Trace lines are "<host thread> <import> <r0> <r1> <r2> <r3>". Host thread ids differ between runs, so threads
are paired by order of first appearance. Prints, per thread pair, the number of matching calls and the first
divergence with context.
"""
import argparse
import collections


def load(path):
    threads = collections.OrderedDict()
    with open(path, errors="replace") as fh:
        for line in fh:
            parts = line.split(None, 1)
            if len(parts) != 2:
                continue
            threads.setdefault(parts[0], []).append(parts[1].rstrip("\n"))
    return list(threads.values())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--context", type=int, default=20)
    args = ap.parse_args()
    ta, tb = load(args.a), load(args.b)
    print(f"threads: {len(ta)} vs {len(tb)}")
    first_divergence = True
    for i, (a, b) in enumerate(zip(ta, tb)):
        n = 0
        while n < len(a) and n < len(b) and a[n] == b[n]:
            n += 1
        if n == len(a) == len(b):
            print(f"thread {i}: identical ({n} calls)")
            continue
        print(f"thread {i}: {n} matching calls of {len(a)} / {len(b)}")
        if first_divergence or i == 0:
            lo = max(0, n - args.context)
            for k in range(lo, min(n + args.context, max(len(a), len(b)))):
                la = a[k] if k < len(a) else "<end>"
                lb = b[k] if k < len(b) else "<end>"
                mark = "  " if la == lb else ">>"
                print(f"  {mark} {k:7d}  {la:60.60s} | {lb:60.60s}")
            first_divergence = False


if __name__ == "__main__":
    main()
