#!/usr/bin/env python3
"""Shared SAT interface: 10 SAT, 20 UNSAT, 2 incomplete/error; validated v-line models."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent
BACKENDS = {
    'minisat': ROOT / 'build/solvers/minisat/minisat',
    'backtracking': ROOT / 'build/solvers/backtracking/backtracking_solver',
}


def read_cnf(path):
    nvars = expected = None
    clauses, clause = [], []
    for line in path.read_text().splitlines():
        words = line.split()
        if not words or words[0] == 'c':
            continue
        if words[0] == 'p':
            if nvars is not None or len(words) != 4 or words[1] != 'cnf':
                raise ValueError('invalid DIMACS header')
            nvars, expected = map(int, words[2:])
            if nvars < 0 or expected < 0:
                raise ValueError('negative DIMACS count')
            continue
        for lit in map(int, words):
            if nvars is None or abs(lit) > nvars:
                raise ValueError('DIMACS literal outside declared range')
            if lit:
                clause.append(lit)
            else:
                clauses.append(clause)
                clause = []
    if nvars is None or clause or len(clauses) != expected:
        raise ValueError('incomplete DIMACS or clause count mismatch')
    return nvars, clauses


def read_model(path, cnf, *, minisat=False, complete=False):
    nvars, clauses = read_cnf(cnf)
    values = {}
    lines = path.read_text().splitlines()
    if minisat:
        if not lines or lines[0].strip() != 'SAT':
            raise ValueError('MiniSat did not write a SAT model')
        lines = lines[1:]
    for line in lines:
        if not minisat:
            if not line.startswith('v '):
                continue
            line = line[2:]
        for lit in map(int, line.split()):
            if lit == 0:
                continue
            var = abs(lit)
            if not 1 <= var <= nvars:
                raise ValueError('model variable outside declared range')
            if var in values and values[var] != (lit > 0):
                raise ValueError('contradictory SAT model')
            values[var] = lit > 0
    # MiniSat may omit unused/undefined variables. Accept a completion only
    # when it satisfies every clause of the original, unsimplified formula.
    if complete:
        for var in range(1, nvars + 1):
            values.setdefault(var, False)
    if len(values) != nvars:
        raise ValueError('incomplete SAT model')
    if any(not any(values[abs(lit)] == (lit > 0) for lit in c) for c in clauses):
        raise ValueError('SAT model does not satisfy generated CNF')
    return values


def solve(cnf, model, *, backend=None, timeout=30):
    backend = backend or os.environ.get('SAT_SOLVER', 'minisat')
    if backend not in BACKENDS:
        raise ValueError(f'unknown SAT solver: {backend}')
    cnf, model = Path(cnf), Path(model)
    if cnf.resolve() == model.resolve():
        raise ValueError('model output must differ from input CNF')
    model.unlink(missing_ok=True)
    read_cnf(cnf)
    model.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sat-', dir=model.parent) as directory:
        raw = Path(directory) / 'model.raw'
        command = [str(BACKENDS[backend]), str(cnf)]
        command += [str(raw)] if backend == 'minisat' else ['--model', str(raw)]
        started = time.perf_counter()
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        elapsed = time.perf_counter() - started
        result.stdout = f'c backend: {backend}\nc solver_wall_seconds: {elapsed:.9f}\n' + result.stdout
        if result.returncode == 10:
            try:
                values = read_model(raw, cnf, minisat=backend == 'minisat', complete=backend == 'minisat')
                model.write_text('s SATISFIABLE\nv ' + ' '.join(
                    str(v if values[v] else -v) for v in sorted(values)) + ' 0\n')
            except (OSError, ValueError) as error:
                model.unlink(missing_ok=True)
                result.returncode = 2
                result.stderr += f'INCOMPLETE: {error}\n'
        elif result.returncode != 20:
            result.stderr += f'INCOMPLETE: {backend} exited with status {result.returncode}\n'
            result.returncode = 2
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cnf', type=Path)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--solver', choices=BACKENDS)
    parser.add_argument('--timeout', type=float, default=float(os.environ.get('SOLVER_TIMEOUT', 30)))
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('timeout must be positive')
    try:
        result = solve(args.cnf, args.model, backend=args.solver, timeout=args.timeout)
        print(result.stdout, end='')
        print(result.stderr, end='', file=sys.stderr)
        return result.returncode
    except subprocess.TimeoutExpired as error:
        for stream, output in ((sys.stdout, error.stdout), (sys.stderr, error.stderr)):
            if output:
                print(output.decode(errors='replace') if isinstance(output, bytes) else output, end='', file=stream)
        print('INCOMPLETE: solver timeout', file=sys.stderr)
        return 2
    except (OSError, ValueError) as error:
        print(f'INCOMPLETE: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
