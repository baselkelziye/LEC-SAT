# Equivalence regression

Run `make equiv-regression` from the project root, or `make equiv TEST=alu`
for one case. Each case contains `lhs.v`, `rhs.v`, and `expected.txt`.
The expected result must be `SAT` (a mismatch exists) or `UNSAT` (equivalent).
Unexpected results, solver errors, and counterexample-decoding errors fail the run.

- `alu`: expects SAT because the left design deliberately drops addition carry.
- `and_equivalent`: expects UNSAT for AND versus its De Morgan expression.

Generated artifacts are under `build/equivalent/<case>/`.
