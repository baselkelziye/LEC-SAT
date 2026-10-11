#!/usr/bin/env python3
"""Check exact depths in increasing order using the selected SAT solver."""
import argparse
import re
from pathlib import Path
import subprocess
import sys
from solver import BACKENDS, read_model, solve

ROOT = Path(__file__).resolve().parent


def nonnegative(value):
    result = int(value)
    if result < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return result


def trace_text(frame_map, values):
    frames = {}
    for line in frame_map.read_text().splitlines():
        t, kind, index, lit, name = line.split("\t", 4)
        lit = int(lit)
        value = int(values[abs(lit)] == (lit > 0))
        frames.setdefault(int(t), []).append(f"{kind}:{name}={value}")
    return "\n".join(f"frame {t}: " + "  ".join(entries) for t, entries in frames.items()) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("aag", type=Path)
    p.add_argument("--bound", type=nonnegative, required=True, help="maximum number of transitions (checks depth 0 too)")
    choice = p.add_mutually_exclusive_group(required=True)
    choice.add_argument("--output", type=nonnegative, help="zero-based ordinary output index used as bad")
    choice.add_argument("--bad", type=nonnegative, help="zero-based native bad-property index")
    p.add_argument("--out-dir", type=Path, default=ROOT / "build/bmc")
    p.add_argument("--timeout", type=float, default=30, help="solver seconds per depth (default 30)")
    p.add_argument("--solver", choices=BACKENDS, help="default: SAT_SOLVER environment or minisat")
    args = p.parse_args()
    if args.timeout <= 0:
        p.error("timeout must be positive")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    # Remove only driver-owned outputs, including depths from a previous longer run.
    for artifact in args.out_dir.iterdir():
        if re.fullmatch(r"depth_\d+\.(cnf|frames|model|log|unroll\.txt|encoder\.log)", artifact.name):
            artifact.unlink()
    trace = args.out_dir / "trace.txt"
    trace.unlink(missing_ok=True)
    kind = "output" if args.output is not None else "bad"
    index = args.output if args.output is not None else args.bad
    for depth in range(args.bound + 1):
        prefix = args.out_dir / f"depth_{depth}"
        generated = subprocess.run([str(ROOT / "build/bmc/bmc_gen"), str(args.aag), str(depth), kind,
                                    str(index), str(prefix)], capture_output=True, text=True)
        prefix.with_suffix(".encoder.log").write_text(generated.stdout + generated.stderr)
        if generated.returncode:
            raise RuntimeError(generated.stderr.strip() or "CNF generation failed")
        model = prefix.with_suffix(".model")
        model.unlink(missing_ok=True)
        try:
            result = solve(prefix.with_suffix(".cnf"), model,
                           backend=args.solver, timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            prefix.with_suffix(".log").write_text(
                (error.stdout or b"").decode(errors="replace") +
                (error.stderr or b"").decode(errors="replace") + "\nTIMEOUT\n")
            print(f"INCOMPLETE: solver timeout at depth {depth}")
            return 2
        prefix.with_suffix(".log").write_text(result.stdout + result.stderr)
        print(f"Unrolling: {prefix.with_suffix('.unroll.txt')}", flush=True)
        if result.returncode == 10:
            values = read_model(model, prefix.with_suffix(".cnf"))
            rendered = trace_text(prefix.with_suffix(".frames"), values)
            trace.write_text(rendered)
            print(f"depth {depth}: SAT — counterexample after {depth} transitions")
            print(rendered, end="")
            print(f"Trace: {trace}")
            return 10
        if result.returncode != 20:
            raise RuntimeError(f"solver failed at depth {depth}: {result.stderr.strip()}")
        print(f"depth {depth}: UNSAT", flush=True)
    print(f"No counterexample through {args.bound} transitions (bounded result only).")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"INCOMPLETE: {error}", file=sys.stderr)
        sys.exit(2)
