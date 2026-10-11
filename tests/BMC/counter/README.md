# First BMC example: a two-bit counter

This is a learning fixture for LEC-SAT's AAG BMC layer. The `bmc_gen` encoder and `bmc.py` driver use MiniSat by default through the shared solver adapter. Select the original solver with `--solver backtracking`. Yosys also supplies a reference bounded check here. The original combinational frontends remain separate.

## Run LEC-SAT BMC

Use `make export` to generate the AAG inputs. The Makefile detects whether
Yosys supports `chformal -lower` and inserts it into the generated native
assertion export script when available. This converts newer `$check` cells
into the assertion cells accepted by the AIGER exporter. Older Yosys versions
already emit `$assert` and do not support that option. The generated script
is `build/export_assert.ys`; the source template remains `export_assert.ys`.

From the LEC-SAT directory:

```sh
make tools
make -C tests/BMC/counter export
python3 bmc.py tests/BMC/counter/build/counter.aag --bound 5 --output 2 --out-dir build/bmc-counter
```

The expected result is UNSAT at depths 0, 1, 2, then SAT at depth 3, with states `00 -> 01 -> 10 -> 11`. SAT exits with status 10 (an expected counterexample here). A bounded pass exits 0; errors/timeouts exit 2 and report INCOMPLETE. `--timeout` sets solver seconds per depth, default 30.

The output directory contains `depth_N.cnf`, `depth_N.frames`, solver logs/models, and `trace.txt` for a counterexample. The frame map has tab-separated frame, kind (`s` state, `i` input, `b` selected bad), index, signed CNF literal, and name. Embedded AAG symbols supply names; otherwise generic names are used. The clock input in the trace is an unused exported port: implicit steps, not its printed value, represent clock edges. Final-frame inputs are included because properties may depend on them; the final enable does not change the state already observed.

For the native assertion export:

```sh
python3 bmc.py tests/BMC/counter/build/counter_assert.aag --bound 5 --bad 0 --out-dir build/bmc-counter-native
```

To inspect one exact-depth encoding without running SAT:

```sh
mkdir -p build/bmc-manual
./build/bmc/bmc_gen tests/BMC/counter/build/counter.aag 3 output 2 build/bmc-manual/depth3
```

Initialization is preserved: omitted AAG latch initialization means zero, explicit 1 means one, and a latch's own literal means unconstrained. No additional zero assumption is added. The CLI selects an output/property by zero-based index; the name `bad` is not automatically selected. The parser supports ASCII AIGER, register initialization, ordinary outputs, and native bad entries. Constraints, justice, and fairness sections are rejected. This first version assumes the input AAG already correctly models the intended synchronous clock steps; it cannot recover or validate RTL clock domains from AAG. Sequential JSON import is not implemented. The plain backtracking solver is intended for small learning examples.

Run semantic regressions with `make test-bmc-unit` from LEC-SAT.

## Run

```sh
make -C /workspace/project/LEC-SAT/tests/BMC/counter
```

Or run `make export` and `make check` from this directory. Outputs and logs go to `build/`.

Tested with Yosys 0.23 (git sha1 7ce5011c24b). The check requires the short bounded proof to succeed and the longer check to find a failure. The displayed `FAIL` for the longer property check is intentional; `-falsify` makes that expected result a successful command.

## Model and property

`counter.v` starts at zero and increments modulo four when enabled. One model transition represents a positive clock edge. The property is deliberately false:

- Good: `q != 3`.
- Bad: `q == 3`.
- Earliest bad state: three transitions after initialization, with enables `1,1,1`.

`initial q = 0` defines the starting state for this example. It is not an assumption that all hardware registers start at zero and it is not a reset sequence.

## Two ways to access the property

### 1. Ordinary output — simplest for your first parser

```verilog
assign bad = (q == 2'b11);
```

`export.ys` exports this as a normal output. In the tested `build/counter.map`:

```text
output 2 0 bad
```

The corresponding symbol is `o2 bad`. Look up output index 2 and read its AIGER literal from the output list. In this run it is literal `28`; IDs can change with tools or optimization, so do not hardcode 28.

After your future frontend resolves that literal into a CNF literal for frame k, add a unit clause requiring it to be true. This asks for a violation at k:

`I(s0) AND T0 AND ... AND T(k-1) AND bad_k`.

If the resolved literal is negative, assert that signed literal, not its absolute value. AIGER literal numbering and DIMACS numbering are different representations; use the frontend's mapping.

The name `bad` has no built-in meaning to your current tool. Selecting this output as the failure condition is a convention the BMC layer must implement. The current combinational equivalence miter would merely compare outputs between two circuits.

### 2. Native assertion — property metadata

With `NATIVE_ASSERT` enabled, the same RTL also contains:

```verilog
always @* begin
    assert(q != 2'b11);
end
```

`export_assert.ys` reads it using `read_verilog -formal -D NATIVE_ASSERT counter.v`.

In `build/counter_assert.json`, the installed Yosys emits a `$assert` cell with:

- `A`: the Boolean condition that should hold (net bit 9 in this run).
- `EN`: whether the assertion is active (`1` in this example).

In Boolean terms its failure condition is `EN AND NOT A`. The assertion is a check, not an assumption forcing `A` true. To find failures, constrain the failure expression true after encoding its logic. With multiple assertions, OR their active failure expressions when asking whether any fails.

Native assertion export produces this extended AIGER header in the tested run:

```text
aag 16 2 2 3 12 1 0 0 0
    M  I L O A  B C J F
```

`B=1` means one bad-state property. Its literal is `32` in this run. This is separate from the three ordinary outputs, which still include the explicit `bad` output. The bad literal occurs after the ordinary output list and before the AND-gate section in this example. A parser must read the format sections correctly; it cannot assume only the original five header counts exist. The accompanying map from this Yosys version has port and latch entries, but no named bad-property entry: the B section is authoritative for locating that native property.

The original combinational AAG parser rejects `L != 0` and has no extended-property parsing. Use the separate `bmc_gen` / `bmc.py` route above for either sequential export.

A combinational immediate assertion was chosen to match the state predicate exactly. A clocked procedural assertion can introduce monitor state and sampling delays; its exported representation must be inspected before comparing depth numbers. This example does not claim support for general SVA sequences.

## Reading the register boundary

The ordinary AAG has two latch records in this run:

```text
6 15
8 27
```

The first value identifies current state; the second is its next-state expression. For each frame, resolve the next-state expression and constrain the next frame's corresponding state bit equal to it. Current-state variables must exist before encoding the combinational graph. Preserve initialization semantics when implementing the parser.

The export also retains `clk` as an input symbol, but AIGER state updates use implicit steps. In this single-clock model one step already means one rising edge; assigning values to the leftover `clk` input does not create or suppress those updates. Validate clock restrictions before extending this approach to arbitrary RTL.

## Results and numbering

`check.ys` checks the native assertion directly with Yosys's SAT engine. For this model, Yosys step 1 is the initial state:

| Mathematical state | Yosys step | q in failing trace |
| --- | --- | --- |
| s0 | 1 | 00 |
| s1 | 2 | 01 |
| s2 | 3 | 10 |
| s3 | 4 | 11 |

- `sat -seq 3 -prove-asserts -verify`: no counterexample through two transitions.
- `sat -seq 4 -prove-asserts -falsify`: counterexample after three transitions.
- `build/check.log`: results and textual trace.
- `build/counterexample.vcd`: waveform trace.

These are bounded checks, not unbounded proofs. The final frame's enable does not affect the already-observed violation.

## Next learning step

Read `counter.map`, then locate the `bad` output literal in `counter.aag`. Separately identify the two latch records. These provide the three things your future BMC layer needs: initial state, next-state logic, and a failure predicate. Do not modify the SAT search algorithm to interpret assertion syntax: the frontend and CNF encoding layer translate it first.

## References

- [Yosys AIGER export](https://yosyshq.readthedocs.io/projects/yosys/en/latest/cmd/index_backends.html#write-aiger-write-design-to-aiger-file): native assertions become bad-state properties; assumptions become constraints.
- [Yosys formal cells](https://yosyshq.readthedocs.io/projects/yosys/en/v0.48/cell/word_formal.html): `$assert` condition and enable inputs.
- Installed `yosys -p 'help write_aiger; help sat'` and generated artifacts were checked for this example; newer versions can emit different intermediate cells or literal IDs.
