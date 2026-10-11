import itertools
import argparse
from functools import lru_cache
import os
from pathlib import Path
import random
import subprocess
from solver import solve

@lru_cache(maxsize=None)
def load_cnf(path):
    clauses = []
    nvars = 0
    with open(path) as f:
        for line in f:
            if line.startswith('c') or not line.strip():
                continue
            if line.startswith('p'):
                nvars = int(line.split()[2])
                continue
            lits = [int(x) for x in line.split()][:-1]
            clauses.append(lits)
    return nvars, clauses

@lru_cache(maxsize=None)
def load_iomap(path):
    entries = []
    with open(path) as f:
        for line in f:
            if line.startswith('c') or not line.strip():
                continue
            d, port, bit, var, pol = line.split()
            entries.append((d, port, int(bit), int(var), pol))
    return entries

def bcp_evaluate(nvars, clauses, forced):
    # forced: dict var -> bool. Propagate until fixpoint; combinational
    # Tseitin CNF should fully determine every variable this way.
    val = dict(forced)
    changed = True
    while changed:
        changed = False
        for cl in clauses:
            unassigned = [l for l in cl if abs(l) not in val]
            sat = any((l > 0) == val.get(abs(l), None) for l in cl if abs(l) in val)
            if sat:
                continue
            if len(unassigned) == 1:
                l = unassigned[0]
                val[abs(l)] = (l > 0)
                changed = True
            elif len(unassigned) == 0:
                raise RuntimeError("clause violated / UNSAT under forced assignment: " + str(cl))
    missing = [v for v in range(1, nvars+1) if v not in val]
    if missing:
        raise RuntimeError(f"{len(missing)} variables not determined by BCP, e.g. {missing[:5]}")
    return val

def alu4_ref(a, b, op, width=4):
    mask = (1 << width) - 1
    a &= mask; b &= mask
    if op == 0:
        s = a + b
        return s & mask, (s >> width) & 1
    elif op == 1:
        s = a - b
        return s & mask, (s >> width) & 1
    elif op == 2:
        return a & b, 0
    else:
        return a | b, 0

def eval_circuit(cnf_path, iomap_path, a, b, op, width=4):
    nvars, clauses = load_cnf(cnf_path)
    io = load_iomap(iomap_path)
    forced = {}
    def set_bits(port, width, value):
        for bit in range(width):
            for d, p, b_, var, pol in io:
                if d == 'i' and p == port and b_ == bit:
                    bitval = (value >> bit) & 1
                    truth = bool(bitval) if pol == '+' else not bool(bitval)
                    forced[var] = truth
    set_bits('a', width, a)
    set_bits('b', width, b)
    set_bits('op', 2, op)
    val = bcp_evaluate(nvars, clauses, forced)
    def get_bits(port, width):
        out = 0
        for bit in range(width):
            for d, p, b_, var, pol in io:
                if d == 'o' and p == port and b_ == bit:
                    v = val[var]
                    if pol == '-':
                        v = not v
                    out |= (1 if v else 0) << bit
        return out
    result = get_bits('result', width)
    cout = get_bits('cout', 1)
    return result, cout

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--width', type=int, default=int(os.environ.get('N', 4)))
    parser.add_argument('--plain', type=Path, default=Path('build/synthesis/plain'))
    parser.add_argument('--optimized', type=Path, default=Path('build/synthesis/optimized'))
    parser.add_argument('--miter', type=Path, default=Path('build/synthesis/miter.cnf'))
    args = parser.parse_args()
    if args.width < 2:
        parser.error('width must be at least 2')
    for directory in (args.plain, args.optimized):
        io = load_iomap(directory / 'alu4.iomap')
        for direction, port, count in [('i', 'a', args.width), ('i', 'b', args.width),
                                       ('i', 'op', 2), ('o', 'result', args.width), ('o', 'cout', 1)]:
            bits = sorted(bit for d, p, bit, _, _ in io if (d, p) == (direction, port))
            if bits != list(range(count)):
                raise RuntimeError(f'{directory}: wrong width for {port}: {bits}')
    root = Path(__file__).resolve().parent
    args.miter.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(root / 'build/equivalence/miter_gen'), str(args.plain / 'alu4.cnf'), str(args.plain / 'alu4.iomap'),
                    str(args.optimized / 'alu4.cnf'), str(args.optimized / 'alu4.iomap'), str(args.miter)], check=True)
    result = solve(args.miter, args.miter.with_suffix('.model'), timeout=float(os.environ.get('SOLVER_TIMEOUT', 30)))
    print(result.stdout, end='')
    print(result.stderr, end='')
    if result.returncode != 20:
        raise RuntimeError('synthesis routes must be equivalent (expected UNSAT)')
    if args.width <= 4:
        vectors = itertools.product(range(1 << args.width), range(1 << args.width), range(4))
        mode = 'exhaustive'
    else:
        mask = (1 << args.width) - 1
        edges = sorted({0, 1, mask, mask - 1, 1 << (args.width - 1), (1 << (args.width - 1)) - 1})
        vectors = list(itertools.product(edges, edges, range(4)))
        rng = random.Random(0)
        vectors += [(rng.getrandbits(args.width), rng.getrandbits(args.width), op)
                    for _ in range(16) for op in range(4)]
        mode = 'directed + seeded samples'
    count = 0
    for a, b, op in vectors:
        expected = alu4_ref(a, b, op, args.width)
        for directory in (args.plain, args.optimized):
            actual = eval_circuit(directory / 'alu4.cnf', directory / 'alu4.iomap', a, b, op, args.width)
            if actual != expected:
                raise RuntimeError(f'N={args.width} a={a} b={b} op={op}: expected {expected}, got {actual}')
        count += 1
    print(f'PASS synthesis: UNSAT miter; reference {mode}, {count} vectors, N={args.width}')


if __name__ == '__main__':
    main()
