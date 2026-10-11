# BMC regression examples

The BMC encoder and driver are independent of the counter. State width, inputs,
gates, and initialization come from the supplied AAG. Property selection uses
an output or native bad index from that model, not a hardcoded counter signal.
The supported scope remains synchronous-step ASCII AIGER safety checking;
constraints, justice, and fairness are rejected.

`make test-bmc` runs the semantic tests plus each `tests/BMC/*/case.json`.
`make test-bmc-unit` retains the faster semantic-only entry point.
MiniSat is the default. Use `make test-bmc SAT_SOLVER=backtracking` to compare
backends, or `--solver backtracking` when invoking `bmc.py` directly.
Both backends use the same CNF validation and decoded trace format.

Regression logs are indexed in `build/tests/README.md`. Each query writes to
`build/tests/bmc/<case>/<query>/`: `depth_N.unroll.txt` explains each frame's
state/input literals, gates, next-state expressions, D-to-Q equality clauses,
and final bad assertion. `depth_N.frames` is the machine-readable literal map,
`depth_N.log` is solver output, and `depth_N.encoder.log` is encoder output.
`trace.txt` contains decoded frame values when SAT is found. Old driver-owned
depth files are removed on rerun, including depths no longer reached.

To add an example, create a directory containing RTL, any export scripts,
a Makefile with an `export` target, and a `case.json` manifest. The runner copies
the fixture (excluding generated `build/`) into `build/tests/bmc/<name>/fixture/`
and runs `make export` there. Export scripts should use paths relative to that
fixture, so they work in the copied directory.

Example manifest:

```json
{
  "queries": [
    {"name": "safety", "aag": "build/design.aag", "kind": "output", "index": 0, "bound": 4, "first_bad": 2}
  ]
}
```

`aag` is relative to the fixture directory. `kind` is `output` or `bad`;
`index` is zero-based. `first_bad` specifies the exact earliest failure depth,
or `null` when every query through `bound` must be UNSAT. A bounded pass is not
an unbounded proof. Query names must be unique and contain only letters,
digits, underscores, or hyphens.

No encoder or runner edits are needed to register another example. Expected
depths belong to the fixture. The counter has ordinary-output, native-bad,
and short-bound queries. Model validation is generic; the semantic counter test
also independently checks its decoded state transitions. For a new design,
an independent behavioral trace check can be added to the semantic tests.

The counter's optional Yosys reference check remains available through its
own `make check`; the regression export target only generates the AAG inputs.
