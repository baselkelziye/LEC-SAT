# cnf_gen

Converts a combinational circuit to DIMACS CNF + a companion `.iomap` file,
via two frontends under `src/combinational/` that share one Tseitin-encoding
backend (`src/common/tseitin.hpp`).

## Build

    make

## `make plain_synth` / `make optimized_synth`

Each target runs its route's synthesis script (requires `yosys` on PATH)
and `cnf_gen`, writing every generated file into a same-named folder --
nothing lands loose in the repo root.

    make plain_synth       # naive: proc+techmap only, no ABC
    # -> plain_synth/synth_netlist.json, plain_synth/alu4.cnf, plain_synth/alu4.iomap

    make optimized_synth   # full synth + aigmap (ABC-optimized)
    # -> optimized_synth/alu4.aag, optimized_synth/alu4.map,
    #    optimized_synth/alu4.cnf, optimized_synth/alu4.iomap

`make clean` removes both folders along with the `cnf_gen` binary.

Override `TOP_V`/`TOP_MODULE`/`YOSYS` on the command line for a different
design, e.g. `make plain_synth TOP_V=foo.v TOP_MODULE=foo`.

## Manual invocation (equivalent to what the targets above do)

    yosys synth_netlist.ys        # -> synth_netlist.json
    ./cnf_gen --from-json synth_netlist.json alu4 alu4_netlist alu4_netlist
    # -> alu4_netlist.cnf, alu4_netlist.iomap

    yosys synth_direct.ys         # -> alu4.aag, alu4.map
    ./cnf_gen --from-aag alu4.aag alu4_aig alu4_aig alu4.map
    # -> alu4_aig.cnf, alu4_aig.iomap

## .iomap format

    <i|o> <port_name> <bit_index> <cnf_var> <+|->

`+`/`-` is the polarity: whether the port bit equals the CNF variable
directly, or its negation.

## verify.py

Brute-force sanity check (unit-propagation based, no real solver needed
yet since these are purely combinational circuits): re-derives every
variable from a fixed primary-input assignment via BCP, and checks both
generated CNFs against each other and against a Python reference model,
for every input combination. Edit `alu4_ref()` for a different circuit.
