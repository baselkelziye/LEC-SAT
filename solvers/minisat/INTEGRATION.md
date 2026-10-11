# LEC-SAT integration

Build from the LEC-SAT root with `make solvers/minisat/minisat` (or `make tools`).
Requires a C++ compiler and zlib development files. The root Makefile compiles
the `simp` executable, retaining preprocessing and model reconstruction.
The supplied upstream Makefiles and LICENSE remain available.

Local C++ compatibility changes:

- Moved `mkLit`'s default argument from its friend declaration to its definition.
- Added whitespace around integer-format macros for C++11 compilation.

`solver.py` invokes MiniSat with a DIMACS path and a temporary result-file path.
It translates SAT results into the project's v-line format only after validating
the completed witness against the input CNF. MiniSat status 0 (indeterminate)
is mapped to an incomplete result, never SAT or UNSAT.

The regression runner tests this interface, equivalence CEX decoding, and BMC
trace semantics. MiniSat's source `core/` directory must remain tracked; the
repository's root core-dump ignore rule is anchored to `/core` for this reason.
