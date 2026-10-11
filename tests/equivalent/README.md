# Equivalence regression

Run `make test-equivalent` from the project root, or `make equiv TEST=alu`
for one case. Each case contains `lhs.v`, `rhs.v`, and `expected.txt`.
The expected result must be `SAT` (a mismatch exists) or `UNSAT` (equivalent).
Unexpected results, solver errors, and counterexample-decoding errors fail the run.

- `alu`: expects SAT because the left design has an incorrect addition carry.
  `test-equivalent` additionally requires an overflow-only counterexample; the
  current `sum_ext[N-1]` fault can produce non-overflow mismatches and fail that
  stricter check. `equiv TEST=alu` only requires SAT and successful decoding.
- `and_equivalent`: expects UNSAT for AND versus its De Morgan expression.

Generated artifacts are under `build/equivalent/<case>/`.
