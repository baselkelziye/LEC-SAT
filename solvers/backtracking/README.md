# Plain backtracking SAT solver

MiniSat is now the default. Select this educational solver with
`make regression SAT_SOLVER=backtracking` or BMC's `--solver backtracking`.

This is the first SAT solver for the project. It intentionally implements only
recursive backtracking:

1. Evaluate the CNF under the current partial assignment.
2. If any clause is false, reject the branch.
3. If every clause is already true, report SAT.
4. Otherwise choose the first unassigned variable.
5. Try `false`, recursively search, then try `true`.
6. If both fail, undo the assignment and backtrack.

It does **not** implement unit propagation, pure-literal elimination, watched
literals, clause learning, VSIDS, or any other DPLL/CDCL optimization.

## Build

From the repository root:

```bash
make build/solvers/backtracking/backtracking_solver
```

Or from this directory:

```bash
make -f backtracking.mk
```

## Run on the ALU miter

From the repository root:

```bash
./build/solvers/backtracking/backtracking_solver build/synthesis/miter.cnf
```

For equivalent circuits the expected result is:

```text
s UNSATISFIABLE
```

For non-equivalent circuits the expected result is:

```text
s SATISFIABLE
v ... 0
```

`SATISFIABLE` means the miter found a model in which an output differs, so the
circuits are not equivalent. `UNSATISFIABLE` means no mismatch is possible, so
the circuits are equivalent.
