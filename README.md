# LEC-SAT
Logical Equivalence using SAT

For bounded safety checking of sequential ASCII AIGER circuits, see
[the counter BMC example](tests/BMC/counter/README.md). Build with `make tools`
and run semantic tests with `make test-bmc-unit`.

Run all regression suites with `make regression`. Run individual suites with
`make test-equivalent`, `make test-synthesis`, or `make test-bmc`.
The full regression requires a C++ compiler, Python 3, Make, Yosys, and zlib
development headers/library (`zlib1g-dev` on Debian/Ubuntu).
Each check prints PASS/FAIL and its log path under `build/tests/`.
Unexpected results and timeouts fail the command; remaining checks still run.
Commands have a 120-second timeout, configurable when invoking
`python3 tests/regression.py --timeout 300` after `make tools`.

The synthesis suite compares both ALU export routes with the Python reference.
Equivalence cases declare their expected SAT/UNSAT result in `expected.txt`.
The BMC suite runs the nine semantic tests and all discovered example manifests,
checking both expected exit status and the complete sequence of depth results.
The BMC driver validates SAT models against their CNF before emitting a trace.
See [adding BMC examples](tests/BMC/README.md).

## Makefile usage

Run these commands from the `LEC-SAT/` directory. Tools are built automatically
before tests. `N=4` and `SAT_SOLVER=minisat` are the defaults.

### Build and inspect the pipeline

| Command | Action |
| --- | --- |
| `make`, `make all`, `make tools` | Build all six executables; no tests. These are intentional aliases. |
| `make plain_synth` | Export the ALU through minimal synthesis to JSON, then CNF and a signal map. |
| `make optimized_synth` | Export the ALU through optimized synthesis to AAG, then CNF and a signal map. |
| `make miter` | Run both synthesis routes and generate their equivalence miter; do not solve it. |
| `make verify` | Run both routes, require an UNSAT miter, and compare both circuits with the software ALU reference. |
| `make equiv TEST=alu` | Compare the faulty and reference ALUs; require SAT and decode the counterexample. |
| `make equiv TEST=and_equivalent` | Compare equivalent AND implementations; require UNSAT. |

Build an individual executable using its path as the target, for example
`make build/equivalence/miter_gen`. `verify` and `equiv` are the underlying
operations used by the test suites; use the suites for saved test reports.
`plain_synth`, `optimized_synth`, `miter`, `verify`, and `equiv` regenerate their
artifacts each time, while executable compilation is incremental.

### Correctness tests

| Command | Coverage |
| --- | --- |
| `make regression` | All checks below, with solver-interface tests run once. |
| `make test-solver` | Solver-interface tests only. |
| `make test-equivalent` | Both RTL fixtures, plus strict validation that the ALU counterexample is an addition overflow with only carry differing. |
| `make test-synthesis` | Synthesis equivalence and software-reference checks via `verify`. |
| `make test-bmc` | BMC semantic unit tests and all counter/toggle fixture queries. |
| `make test-bmc-unit` | BMC semantic unit tests only, with direct unittest output. |

Individual design suites do not repeat solver-interface tests. `test-bmc-unit`
is a quick subset of `test-bmc`. The redundant `equiv-regression` command was
removed in favor of `test-equivalent`; `bmc-test` was renamed to `test-bmc-unit`.

The full regression has 11 reported checks, some containing multiple unit tests:

- Solver interface: SAT/UNSAT, empty formulas/clauses, unused variables, witness
  validation, stale-model removal, errors, and timeouts.
- RTL equivalence: the faulty ALU must be SAT; equivalent AND circuits must be
  UNSAT. SAT is an expected success for an intentionally faulty design.
- Synthesis: plain versus optimized must be UNSAT for all inputs. Software
  reference checks enumerate all inputs at `N <= 4`; larger widths use directed
  cases and deterministic random samples.
- BMC semantics: initialization, transitions, frame connections, changing inputs,
  constants, native properties, depth handling, traces, and invalid witnesses.
- Counter preparation and three queries: failure first at depth 3 for both an
  ordinary output and a native bad property; bounded UNSAT through depth 2.
- Toggle preparation and one query: the selected output first becomes high at
  depth 1. Bounded UNSAT establishes safety only through the checked bound.

`make equiv TEST=alu` checks SAT and decodes the model, but does not run the
strict overflow-specific assertion from `test-equivalent`. The current faulty
RTL uses `sum_ext[N-1]` as carry, so it can also disagree without overflow;
the strict check can fail (as in the saved N=4 benchmark). Command consolidation
does not change that fixture.

```sh
make regression N=8
make test-equivalent N=16
make test-synthesis N=32
make test-bmc
make test-solver SAT_SOLVER=backtracking
make test-bmc-unit SAT_SOLVER=backtracking
```

### Width benchmarks

`make benchmark` runs the design suites at each width, skipping solver-interface
and BMC semantic unit tests. Each run has isolated artifacts and CSV, JSON, and
Markdown reports under `build/benchmarks/<timestamp>/`. `latest.txt` points to
the latest completed report. Compilation time is excluded; total time includes
synthesis, encoding, solving, and validation. Solver time measures completed
solver subprocesses, including startup and parsing. Failed runs are not
successful performance measurements.

The width defaults are defined only in `tests/benchmark.py`, so Make and direct
Python invocation agree: `2 4 8 16 32 64 128 256 512 1024 2048 4192 8384`.
The existing 4192/8384 values are preserved. Defaults are one repetition and
120 seconds per test command, including synthesis, rather than just SAT search.

```sh
make benchmark BENCH_WIDTHS="2 4 8 16 32 64" BENCH_REPEATS=3 BENCH_TIMEOUT=300
```

| Setting | Purpose/default |
| --- | --- |
| `N` | Single-run design width, at least 2; default 4. Benchmarks use their width list instead. |
| `SAT_SOLVER` | `minisat` (default) or `backtracking`. |
| `TEST` | Fixture selected by `equiv`; default `alu`. |
| `BENCH_WIDTHS` | Optional override of the Python benchmark's default width list. |
| `BENCH_REPEATS`, `BENCH_TIMEOUT` | Benchmark repetitions (1) and per-command seconds (120). |
| `PLAIN_DIR`, `OPT_DIR` | Synthesis directories; `build/synthesis/plain` and `build/synthesis/optimized`. |
| `SYNTH_MITER` | Shared output path for `miter` and `verify`; `build/synthesis/miter.cnf`. Replaces `MITER_CNF`. |
| `EQUIV_BUILD_ROOT` | Equivalence fixture artifacts; `build/equivalent`. |
| `CXX`, `CXXFLAGS`, `PYTHON`, `YOSYS` | Compiler/build or command overrides where supported; regression requires `yosys` on PATH. |

For a different regression timeout, build tools and run
`python3 tests/regression.py --timeout 300`. Standalone `verify` and `equiv`
use `SOLVER_TIMEOUT` (default 30 seconds) for their solver calls; regression
sets it to its own command timeout.

### Cleanup and fixture commands

| Command | Action |
| --- | --- |
| `make clean-tests` | Remove `build/tests`, `build/equivalent`, `build/synthesis`, and local BMC fixture builds. Keep binaries and benchmarks. |
| `make clean` | Remove the entire `build/` tree, **including benchmark reports**, fixture builds, Python caches, and legacy outputs. |
| `make -C tests/BMC/counter export` | Generate the counter AAG fixtures. |
| `make -C tests/BMC/counter check` | Independent Yosys reference check: safety through two transitions and failure at three. Not part of `regression`. |
| `make -C tests/BMC/counter` | Export and run the Yosys reference check. |
| `make -C tests/BMC/toggle export` | Generate the toggle AAG fixture. |
| `make -C tests/BMC/counter clean` / `make -C tests/BMC/toggle clean` | Remove that fixture's local build directory. |
| `make -C solvers/backtracking -f backtracking.mk` | Build only the backtracker; also supports `clean`. |

Custom output paths outside the default build directories must be cleaned
separately. For equivalence pipeline debugging, after `make tools`, use
`make -C tests/equivalent -f equiv.mk TARGET TEST=alu`, where `TARGET` is `check`,
`lhs`, `rhs`, `miter`, `solve`, `run`, or `clean`. `check` validates fixture inputs;
`lhs`/`rhs` export one side; `miter` exports both and combines them; `solve`/`run`
execute the full comparison. Prefer the root commands for ordinary use.

## SAT solver selection

MiniSat is the default for equivalence and BMC. `make tools` builds the vendored
simplifying solver at `build/solvers/minisat/minisat` and the original backtracker.

```sh
make regression                         # MiniSat, including solver-interface checks
make equiv TEST=alu                     # MiniSat + decoded counterexample
make regression SAT_SOLVER=backtracking # compare with the original solver
```

Python entry points also honor the `SAT_SOLVER` environment variable. BMC
accepts `--solver minisat` or `--solver backtracking` to override it.
The shared adapter can solve a standalone DIMACS file:

```sh
python3 solver.py input.cnf --model model.txt --solver minisat --timeout 30
```

The adapter returns 10 for SAT, 20 for UNSAT, and 2 for errors or incomplete
results. It validates every SAT model against the original CNF and writes the
existing `s SATISFIABLE` / `v ... 0` model format for CEX and trace decoding.
MiniSat's omitted variables receive a value only if the completed assignment
satisfies the original CNF. Invalid witnesses never produce a model file.
Solver logs identify the backend and retain its statistics; output paths remain
unchanged. Different solvers may find different valid counterexamples.
BMC still solves a separate query at each depth; incremental solving is not used.

Equivalence artifacts live under `build/equivalent/`, synthesis artifacts under
`build/synthesis/`, and regression logs and BMC example artifacts under `build/tests/`.

## Finding current results

Run `make regression`, then open `build/tests/README.md` for the latest run's
PASS/FAIL table and links to every test log. Logs include a UTC start time and
the command used. Individual suites update this index with only their checks.

| Result | Location |
| --- | --- |
| Equivalence log, including decoded CEX | `build/tests/equivalent/<case>.log` |
| Equivalence CEX and SAT model | `build/equivalent/<case>/cex.txt`, `model.txt` |
| Synthesis verification log | `build/tests/synthesis/alu.log` |
| BMC depth results and counterexample frames | `build/tests/bmc/<case>/<query>.log` |
| BMC readable unrolling at depth N | `build/tests/bmc/<case>/<query>/depth_N.unroll.txt` |
| BMC solver log, CNF, and literal map | Same directory: `depth_N.log`, `.cnf`, `.frames` |
| BMC decoded counterexample | Same directory: `trace.txt` |

For example, inspect `build/tests/bmc/counter/output/depth_3.unroll.txt`
for all four frames and `build/tests/bmc/counter/output/trace.txt` for the
counterexample. An unrolling report describes constraints, including D-to-Q
equality clauses; a trace describes a satisfying assignment. UNSAT has no trace.

`make equiv TEST=alu` displays the decoded CEX directly; `make test-equivalent`
also saves its complete log. Old CEX/model files are removed before synthesis,
so a failed case cannot leave a previous counterexample looking current.
BMC replaces its own depth artifacts on each invocation; use separate output
directories when comparing runs.

Generated build results and binaries are not source files and are ignored.
`make clean` removes root build outputs and tools; `make tools` rebuilds them.
After transferring a workspace between machines, use `make -B tools` to avoid
reusing binaries linked against another machine's libraries.

## Source layout

```text
src/common/                 Shared CNF builder and DIMACS utilities
src/combinational/          Combinational JSON/AAG frontends and main
src/equivalence/            Miter generator and counterexample decoder
src/bmc/                    Sequential AAG parser, encoder, and main
src/solvers/backtracking/   SAT solver implementation
solvers/minisat/            Vendored MiniSat sources and license
solver.py                  Shared solver invocation and witness validation
third_party/json.hpp       Vendored JSON dependency
```

Build all tools with `make tools`. Generated executables live under:

```text
build/combinational/cnf_gen
build/equivalence/miter_gen
build/equivalence/cex_decoder
build/bmc/bmc_gen
build/solvers/backtracking/backtracking_solver
build/solvers/minisat/minisat
```

Synthesis artifacts live in `build/synthesis/{plain,optimized}`; equivalence
fixtures in `build/equivalent`; regression logs in `build/tests`; and benchmarks
in `build/benchmarks`. Standalone BMC fixtures use their own local `build/`.
Python drivers and Yosys scripts remain at their existing source paths.

`make clean` removes all generated outputs, including benchmark reports, fixture
build directories, and Python caches. `make clean-tests` removes test/synthesis
outputs while retaining binaries and benchmark reports. Run `make regression`
to check the rebuilt tools.
