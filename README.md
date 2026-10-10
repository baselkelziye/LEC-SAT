# LEC-SAT
Logical Equivalence using SAT

For bounded safety checking of sequential ASCII AIGER circuits, see
[the counter BMC example](tests/BMC/counter/README.md). Build with `make tools`
and run semantic tests with `make bmc-test`.

Run all regression suites with `make regression`. Run individual suites with
`make test-equivalent`, `make test-synthesis`, or `make test-bmc`.
The full regression requires a C++ compiler, Python 3, Make, and Yosys.
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

Existing equivalence and synthesis commands
still write their detailed artifacts to their original locations; the new
regression logs and BMC example artifacts live under `build/tests/`.

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
third_party/json.hpp       Vendored JSON dependency
```

Executables retain their existing locations: `cnf_gen`, `miter_gen`, and
`bmc_gen` at the root, `miter_circuit/cex_decoder`, and
`solvers/backtracking/backtracking_solver`. Python drivers and Yosys scripts
also retain their existing paths. Build all tools with `make tools` and verify
them with `make regression`.
