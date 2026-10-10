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

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=['all', 'equivalent', 'synthesis', 'bmc'], default='all')
    parser.add_argument('--timeout', type=float, default=120, help='seconds per command')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('timeout must be positive')
    for tool in ('make', 'yosys'):
        if not shutil.which(tool):
            parser.error(f'required tool missing: {tool}')
    output = ROOT / 'build/tests'
    output.mkdir(parents=True, exist_ok=True)
    results = []

    def run(name, command, cwd=ROOT, expected=0, depths=None):
        log = output / (name + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        try:
            with log.open('w') as stream:
                stream.write(f'Started: {datetime.now(timezone.utc).isoformat()}\nCommand: {command!r}\nDirectory: {cwd}\n\n')
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
            if depths is not None:
                observed = re.findall(r'^depth (\d+): (UNSAT|SAT)\b', log.read_text(), re.M)
                passed = passed and observed == depths
        except OSError as error:
            log.write_text(str(error) + '\n')
            passed = False
        results.append((name, passed))
        print(f'{"PASS" if passed else "FAIL"} {name} (log: {log.relative_to(ROOT)})', flush=True)
        return passed

    if args.suite in ('all', 'equivalent'):
        cases = sorted((ROOT / 'tests/equivalent').glob('*/expected.txt'))
        if not cases:
            raise ValueError('no equivalence cases discovered')
        for case in cases:
            run(f'equivalent/{case.parent.name}', ['make', 'equiv', f'TEST={case.parent.name}'])
    if args.suite in ('all', 'synthesis'):
        run('synthesis/alu', ['make', 'verify'])
    if args.suite in ('all', 'bmc'):
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
            if not run(f'bmc/{name}/prepare', ['make', 'export'], cwd=work):
                continue
            for query in queries:
                failure = query['first_bad']
                last = query['bound'] if failure is None else failure
                depths = [(str(t), 'SAT' if t == failure else 'UNSAT') for t in range(last + 1)]
                destination = output / 'bmc' / name / query['name']
                run(f'bmc/{name}/{query["name"]}',
                    [sys.executable, str(ROOT / 'bmc.py'), str(work / query['aag']),
                     '--bound', str(query['bound']), '--' + query['kind'], str(query['index']),
                     '--out-dir', str(destination)],
                    expected=0 if failure is None else 10, depths=depths)
    failed = sum(not passed for _, passed in results)
    summary = output / 'README.md'
    lines = ['# Latest regression run', '',
             f'Completed: {datetime.now(timezone.utc).isoformat()} (suite: {args.suite})', '',
             '| Check | Result | Log |', '| --- | --- | --- |']
    for name, passed in results:
        lines.append(f'| {name} | {"PASS" if passed else "FAIL"} | [log]({name}.log) |')
    lines += ['', 'Equivalence artifacts: `../equivalent/<case>/cex.txt` (SAT only).',
              'BMC artifacts: `bmc/<case>/<query>/depth_N.unroll.txt`, `depth_N.log`,',
              '`depth_N.cnf`, `depth_N.frames`, and `trace.txt` (SAT only).',
              'Only checks listed above belong to this run; other suite logs may be older.', '']
    summary.write_text('\n'.join(lines))
    print(f'Results index: {summary.relative_to(ROOT)}')
    print(f'{len(results) - failed} passed, {failed} failed')
    return int(bool(failed))


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'FAIL regression configuration: {error}', file=sys.stderr)
        sys.exit(1)
