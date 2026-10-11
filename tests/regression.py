"""Run project regressions and preserve logs for each check."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def check_alu_cex(path, width):
    text = path.read_text()
    inputs = {name: (int(bits), int(value, 2)) for name, bits, value in
              re.findall(r'^  (a|b|op) = (\d+)\x27b([01]+)$', text, re.M)}
    outputs = {name: (int(w1), int(v1, 2), int(w2), int(v2, 2))
               for name, w1, v1, w2, v2 in re.findall(
                   r'^  (cout|result):\n    lhs = (\d+)\x27b([01]+)\n    rhs = (\d+)\x27b([01]+)', text, re.M)}
    if set(inputs) != {'a', 'b', 'op'} or set(outputs) != {'cout', 'result'}:
        raise ValueError('missing decoded ALU signals')
    a, b = inputs['a'][1], inputs['b'][1]
    low = (a + b) & ((1 << width) - 1)
    if (inputs['a'][0] != width or inputs['b'][0] != width or inputs['op'] != (2, 0)
            or a + b < (1 << width) or outputs['result'] != (width, low, width, low)
            or outputs['cout'] != (1, 0, 1, 1)):
        raise ValueError('ALU counterexample is not an N-bit addition carry-only mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=['all', 'solver', 'equivalent', 'synthesis', 'bmc'], default='all')
    parser.add_argument('--timeout', type=float, default=120, help='seconds per command')
    parser.add_argument('--width', type=int, default=int(os.environ.get('N', 4)))
    parser.add_argument('--out-dir', type=Path, default=ROOT / 'build/tests')
    parser.add_argument('--designs-only', action='store_true', help='skip width-independent unit tests')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('timeout must be positive')
    if args.width < 2:
        parser.error('width must be at least 2')
    os.environ['N'] = str(args.width)
    os.environ['SOLVER_TIMEOUT'] = str(args.timeout)
    for tool in ('make', 'yosys'):
        if not shutil.which(tool):
            parser.error(f'required tool missing: {tool}')
    output = args.out_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    results = []
    records = []

    def run(name, command, cwd=ROOT, expected=0, depths=None, artifacts=None):
        log = output / (name + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.perf_counter()
        code = None
        try:
            with log.open('w') as stream:
                stream.write(f'Started: {datetime.now(timezone.utc).isoformat()}\nCommand: {command!r}\nDirectory: {cwd}\nSolver: {os.environ.get("SAT_SOLVER", "minisat")}\nN: {args.width}\n\n')
                stream.flush()
                process = subprocess.Popen(command, cwd=cwd, stdout=stream,
                                           stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    code = process.wait(timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                    stream.write('\nTIMEOUT\n')
                    code = None
            passed = code == expected
            if passed and name == 'equivalent/alu':
                try:
                    check_alu_cex(artifacts / 'cex.txt', args.width)
                except (OSError, ValueError) as error:
                    with log.open('a') as stream:
                        stream.write(f'FAIL decoded overflow check: {error}\n')
                    passed = False
            if depths is not None:
                observed = re.findall(r'^depth (\d+): (UNSAT|SAT)\b', log.read_text(), re.M)
                passed = passed and observed == depths
        except OSError as error:
            log.write_text(str(error) + '\n')
            passed = False
        results.append((name, passed))
        elapsed = time.perf_counter() - started
        contents = log.read_text()
        timing_text = contents
        if depths is not None and artifacts:
            timing_text = '\n'.join(p.read_text() for p in artifacts.glob('depth_*.log'))
        times = [float(x) for x in re.findall(r'^c solver_wall_seconds: ([\d.]+)$', timing_text, re.M)]
        sizes = []
        if artifacts:
            for cnf in artifacts.rglob('*.cnf'):
                with cnf.open() as stream:
                    for line in stream:
                        if line.startswith('p cnf '):
                            sizes.append(tuple(map(int, line.split()[2:])))
                            break
        outcome = 'ERROR' if not passed else 'PASS'
        if passed and depths is not None:
            outcome = 'SAT' if expected == 10 else 'BOUNDED_UNSAT'
        elif passed and ('Result: UNSAT' in contents or 'PASS synthesis: UNSAT' in contents):
            outcome = 'UNSAT'
        elif passed and 'Result: SAT' in contents:
            outcome = 'SAT'
        records.append(dict(case=name, width=args.width, status='PASS' if passed else 'FAIL',
                            outcome=outcome, exit_code=code, total_seconds=elapsed,
                            solver_seconds=sum(times) if times else None, solver_calls=len(times),
                            cnf_variables=max((x[0] for x in sizes), default=None),
                            cnf_clauses=max((x[1] for x in sizes), default=None), log=str(log)))
        print(f'{"PASS" if passed else "FAIL"} {name} N={args.width} ({elapsed:.3f}s; log: {log})', flush=True)
        return passed

    if not args.designs_only and args.suite in ('all', 'solver'):
        run('solver/interface', [sys.executable, 'tests/test_solver.py'])

    if args.suite in ('all', 'equivalent'):
        cases = sorted((ROOT / 'tests/equivalent').glob('*/expected.txt'))
        if not cases:
            raise ValueError('no equivalence cases discovered')
        for case in cases:
            artifacts = Path(os.environ.get('EQUIV_BUILD_ROOT', ROOT / 'build/equivalent')) / case.parent.name
            run(f'equivalent/{case.parent.name}', ['make', 'equiv', f'TEST={case.parent.name}', f'N={args.width}'], artifacts=artifacts)
    if args.suite in ('all', 'synthesis'):
        artifacts = Path(os.environ.get('SYNTH_MITER', ROOT / 'build/synthesis/miter.cnf')).parent
        run('synthesis/alu', ['make', 'verify', f'N={args.width}'], artifacts=artifacts)
    if args.suite in ('all', 'bmc'):
        if not args.designs_only:
            run('bmc/semantics', [sys.executable, 'tests/BMC/test_bmc.py'])
        manifests = sorted((ROOT / 'tests/BMC').glob('*/case.json'))
        if not manifests:
            raise ValueError('no BMC example manifests discovered')
        for manifest in manifests:
            case = json.loads(manifest.read_text())
            queries = case['queries']
            if not queries:
                raise ValueError(f'{manifest}: queries must not be empty')
            names = set()
            for query in queries:
                name, bound, failure = query['name'], query['bound'], query['first_bad']
                if not re.fullmatch(r'[a-zA-Z0-9_-]+', name) or name in names:
                    raise ValueError(f'{manifest}: invalid or duplicate query name')
                names.add(name)
                if type(bound) is not int or bound < 0 or (failure is not None and
                        (type(failure) is not int or not 0 <= failure <= bound)):
                    raise ValueError(f'{manifest}: invalid bound or first_bad')
                if query['kind'] not in ('output', 'bad') or type(query['index']) is not int or query['index'] < 0:
                    raise ValueError(f'{manifest}: invalid property selection')
            name = manifest.parent.name
            work = output / 'bmc' / name / 'fixture'
            # Copy source inputs only; exports are regenerated in this workspace.
            shutil.copytree(manifest.parent, work, dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns('build', '__pycache__'))
            if not run(f'bmc/{name}/prepare', ['make', 'export', f'N={args.width}'], cwd=work):
                continue
            for query in queries:
                failure = query['first_bad']
                last = query['bound'] if failure is None else failure
                depths = [(str(t), 'SAT' if t == failure else 'UNSAT') for t in range(last + 1)]
                destination = output / 'bmc' / name / query['name']
                index = query['index']
                if 'output_name' in query:
                    symbols = re.findall(r'^o(\d+) (.+)$', (work / query['aag']).read_text(), re.M)
                    matches = [int(i) for i, symbol in symbols if symbol == query['output_name']]
                    if query['kind'] != 'output' or len(matches) != 1:
                        raise ValueError(f'{manifest}: output symbol not found or ambiguous')
                    index = matches[0]
                run(f'bmc/{name}/{query["name"]}',
                    [sys.executable, str(ROOT / 'bmc.py'), str(work / query['aag']),
                     '--bound', str(query['bound']), '--' + query['kind'], str(index),
                     '--out-dir', str(destination), '--timeout', str(args.timeout)],
                    expected=0 if failure is None else 10, depths=depths, artifacts=destination)
    failed = sum(not passed for _, passed in results)
    summary = output / 'README.md'
    lines = ['# Latest regression run', '',
             f'Completed: {datetime.now(timezone.utc).isoformat()} (suite: {args.suite}, N={args.width})', '',
             '| Check | Result | Log |', '| --- | --- | --- |']
    for name, passed in results:
        lines.append(f'| {name} | {"PASS" if passed else "FAIL"} | [log]({name}.log) |')
    lines += ['', 'Equivalence artifacts: `../equivalent/<case>/cex.txt` (SAT only).',
              'BMC artifacts: `bmc/<case>/<query>/depth_N.unroll.txt`, `depth_N.log`,',
              '`depth_N.cnf`, `depth_N.frames`, and `trace.txt` (SAT only).',
              'Only checks listed above belong to this run; other suite logs may be older.', '']
    summary.write_text('\n'.join(lines))
    (output / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
    print(f'Results index: {summary}')
    print(f'{len(results) - failed} passed, {failed} failed')
    return int(bool(failed))


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'FAIL regression configuration: {error}', file=sys.stderr)
        sys.exit(1)
