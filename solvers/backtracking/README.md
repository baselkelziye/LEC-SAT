# Plain backtracking SAT solver

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
g++ -std=c++17 -O2 -Wall -Wextra \
    -o solvers/backtracking/backtracking_solver \
    solvers/backtracking/backtracking.cpp
```

Or from this directory:

```bash
make
```

## Run on the ALU miter

From the repository root:

```bash
./solvers/backtracking/backtracking_solver miter_circuit/alu4_miter.cnf
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