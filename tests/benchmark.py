"""Benchmark every parameterized design in isolated per-width workspaces."""
import argparse
import csv
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--widths', nargs='+', type=int, default=[2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4192, 8384])
    parser.add_argument('--repeats', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=120, help='seconds per test command')
    args = parser.parse_args()
    if any(n < 2 for n in args.widths) or len(set(args.widths)) != len(args.widths):
        parser.error('widths must be unique integers >= 2')
    if args.repeats < 1 or args.timeout <= 0:
        parser.error('repeats and timeout must be positive')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = ROOT / 'build/benchmarks' / stamp
    output.mkdir(parents=True)
    backend = os.environ.get('SAT_SOLVER', 'minisat')
    metadata = dict(started=stamp, solver=backend, widths=args.widths, repeats=args.repeats,
                    timeout=args.timeout, platform=platform.platform(), cpu_count=os.cpu_count(),
                    timing='wall seconds; solver subprocess time includes startup/parsing/preprocessing/search/output; total includes synthesis, encoding, validation and decoding; build tools excluded')
    (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    fields = ['width', 'repeat', 'case', 'status', 'outcome', 'total_seconds', 'solver_seconds',
              'solver_calls', 'cnf_variables', 'cnf_clauses', 'exit_code', 'log']
    rows = []
    failed = False
    for width in args.widths:
        for repeat in range(1, args.repeats + 1):
            work = output / f'N{width}' / f'run{repeat}'
            work.mkdir(parents=True)
            env = dict(os.environ, N=str(width), EQUIV_BUILD_ROOT=str(work / 'equivalent'),
                       PLAIN_DIR=str(work / 'synthesis/plain'), OPT_DIR=str(work / 'synthesis/optimized'),
                       SYNTH_MITER=str(work / 'synthesis/miter.cnf'))
            command = [sys.executable, str(ROOT / 'tests/regression.py'), '--width', str(width),
                       '--out-dir', str(work / 'tests'), '--designs-only', '--timeout', str(args.timeout)]
            print(f'Benchmark N={width}, repeat={repeat}, solver={backend}', flush=True)
            with (work / 'run.log').open('w') as stream:
                result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
            failed |= result.returncode != 0
            summary = work / 'tests/results.json'
            if summary.exists():
                records = json.loads(summary.read_text())
            else:
                records = [dict(width=width, case='runner', status='FAIL', outcome='ERROR',
                                exit_code=result.returncode, log=str(work / 'run.log'))]
            for record in records:
                record['repeat'] = repeat
                rows.append(record)
                total = record.get('total_seconds')
                seconds = record.get('solver_seconds')
                print(f'  {record["status"]} {record["case"]}: {record["outcome"]}; '
                      f'total={total:.6f}s; solver={seconds:.6f}s' if total is not None and seconds is not None
                      else f'  {record["status"]} {record["case"]} (see {record["log"]})', flush=True)
            # Save after every width/repetition so completed timings survive an interrupted sweep.
            with (output / 'results.csv').open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                writer.writerows(rows)
            (output / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
    lines = ['# Width benchmark', '', f'Solver: {backend}. Widths: {args.widths}. Repeats: {args.repeats}.', '',
             'Times are wall seconds. Solver time sums completed solver subprocesses; it includes startup,',
             'DIMACS parsing, preprocessing, search and result writing. Total also includes test setup,',
             'encoding and checking. BMC prepare rows time export separately; BMC query rows exclude export.',
             'Tool compilation is excluded. Failed/timed-out rows are not successful performance measurements.',
             'CNF counts are maxima over the test artifacts (for BMC, the largest checked depth).', '',
             '| N | Run | Case | Status | Result | Total (s) | Solver (s) | Log |',
             '| --- | --- | --- | --- | --- | --- | --- | --- |']
    for row in rows:
        total = f'{row["total_seconds"]:.6f}' if row.get('total_seconds') is not None else '—'
        seconds = f'{row["solver_seconds"]:.6f}' if row.get('solver_seconds') is not None else '—'
        link = Path(row['log']).relative_to(output)
        lines.append(f'| {row["width"]} | {row["repeat"]} | {row["case"]} | {row["status"]} | {row["outcome"]} | {total} | {seconds} | [log]({link}) |')
    (output / 'README.md').write_text('\n'.join(lines) + '\n')
    (output.parent / 'latest.txt').write_text(str(output) + '\n')
    print(f'Benchmark report: {output / "README.md"}')
    print(f'CSV: {output / "results.csv"}')
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
