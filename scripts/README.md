# Shared Yosys scripts

These source scripts are required by the project Makefiles:

| Script | Purpose | Root Make targets |
| --- | --- | --- |
| `eqy_test.ys` | Synthesize either RTL equivalence fixture and export AAG plus its port map. | `equiv`, `test-equivalent`, `regression`, `benchmark` |
| `synth_netlist.ys` | Minimal synthesis and JSON export for the plain CNF frontend. | `plain_synth`, `miter`, `verify`, `test-synthesis`, `regression`, `benchmark` |
| `synth_direct.ys` | Optimized synthesis, AIG conversion, and AAG export with a port map. | `optimized_synth`, `miter`, `verify`, `test-synthesis`, `regression`, `benchmark` |

Run the Make targets from the project root. They copy RTL into build directories,
generate width-specific scripts there, and run Yosys inside those directories.
Relative paths inside a Yosys script refer to its working directory, so moving
these source templates does not change where generated files are written.
`make clean` preserves this source directory and removes generated script copies.
BMC-specific scripts remain alongside their fixtures in `tests/BMC/`.
