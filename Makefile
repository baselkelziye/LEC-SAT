CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

YOSYS ?= yosys
PYTHON ?= python3
export SAT_SOLVER ?= minisat
export N ?= 4
export PLAIN_DIR ?= $(CURDIR)/build/synthesis/plain
export OPT_DIR ?= $(CURDIR)/build/synthesis/optimized
export EQUIV_BUILD_ROOT ?= $(CURDIR)/build/equivalent
export SYNTH_MITER ?= $(CURDIR)/build/synthesis/miter.cnf
# Leave empty to use the defaults defined in tests/benchmark.py.
BENCH_WIDTHS ?=
BENCH_REPEATS ?= 1
BENCH_TIMEOUT ?= 120


# ==================================================
# Global configuration
# ==================================================

TOP_V ?= alu4.v
TOP_MODULE ?= alu4

TEST ?= alu


# ==================================================
# Tool paths
# ==================================================

CNF_GEN := build/combinational/cnf_gen
MITER_GEN := build/equivalence/miter_gen

SOLVER_DIR := solvers/backtracking
SOLVER_MAKEFILE := backtracking.mk
SOLVER_SRC := src/solvers/backtracking/backtracking.cpp
SOLVER := build/solvers/backtracking/backtracking_solver

CEX_DECODER := build/equivalence/cex_decoder

EQUIV_DIR := tests/equivalent
EQUIV_MAKEFILE := equiv.mk


# ==================================================
# Default
#
# Just build the project tools.
# ==================================================

all: tools


tools: $(CNF_GEN) $(MITER_GEN) $(SOLVER) $(CEX_DECODER) build/bmc/bmc_gen build/solvers/minisat/minisat

MINISAT_SOURCES := solvers/minisat/simp/Main.cc solvers/minisat/simp/SimpSolver.cc solvers/minisat/core/Solver.cc solvers/minisat/utils/Options.cc solvers/minisat/utils/System.cc
MINISAT_HEADERS := $(wildcard solvers/minisat/*/*.h)

build/solvers/minisat/minisat: $(MINISAT_SOURCES) $(MINISAT_HEADERS)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Isolvers/minisat -std=c++11 -O3 -DNDEBUG -D__STDC_LIMIT_MACROS -D__STDC_FORMAT_MACROS $(MINISAT_SOURCES) -lz -o $@

build/bmc/bmc_gen: src/bmc/bmc_main.cpp src/bmc/bmc_aag.hpp src/common/tseitin.hpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Isrc/common -Ithird_party $(CXXFLAGS) -o $@ src/bmc/bmc_main.cpp

test-bmc-unit: build/bmc/bmc_gen $(SOLVER) build/solvers/minisat/minisat
	$(PYTHON) tests/BMC/test_bmc.py

.PHONY: test-bmc-unit

regression: tools
	$(PYTHON) tests/regression.py

benchmark: tools
	$(PYTHON) tests/benchmark.py $(if $(strip $(BENCH_WIDTHS)),--widths $(BENCH_WIDTHS)) --repeats $(BENCH_REPEATS) --timeout $(BENCH_TIMEOUT)

.PHONY: benchmark

test-solver test-equivalent test-synthesis test-bmc: tools
	$(PYTHON) tests/regression.py --suite $(patsubst test-%,%,$@)

.PHONY: regression test-solver test-equivalent test-synthesis test-bmc


# ==================================================
# Tools
# ==================================================

$(CNF_GEN): \
	src/combinational/main.cpp \
	src/common/tseitin.hpp \
	src/combinational/aag_frontend.hpp \
	src/combinational/json_frontend.hpp \
	third_party/json.hpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Isrc/common -Ithird_party $(CXXFLAGS) -o $@ src/combinational/main.cpp


$(MITER_GEN): \
	src/equivalence/miter.cpp \
	src/common/cnf.hpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Isrc/common -Ithird_party $(CXXFLAGS) -o $@ src/equivalence/miter.cpp


$(CEX_DECODER): \
	src/equivalence/cex.cpp \
	src/common/cnf.hpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Isrc/common -Ithird_party $(CXXFLAGS) -o $@ src/equivalence/cex.cpp


$(SOLVER): \
	$(SOLVER_SRC) \
	$(SOLVER_DIR)/$(SOLVER_MAKEFILE) \
	src/common/cnf.hpp
	$(MAKE) \
		-C $(SOLVER_DIR) \
		-f $(SOLVER_MAKEFILE)


# ==================================================
# Original synthesis experiment
#
# Same alu4.v:
#   direct/unoptimized synthesis
#        vs
#   optimized synthesis
# ==================================================

plain_synth: $(CNF_GEN) scripts/synth_netlist.ys
	mkdir -p $(PLAIN_DIR)
	@$(PYTHON) -c 'assert int("$(N)") >= 2, "N must be at least 2"'
	cp $(TOP_V) $(PLAIN_DIR)/alu4.v
	sed 's/^chparam -set N 4 /chparam -set N $(N) /' scripts/synth_netlist.ys > $(PLAIN_DIR)/synth.ys

	cd $(PLAIN_DIR) && \
		$(YOSYS) -q -s synth.ys

	./$(CNF_GEN) \
		--from-json \
		$(PLAIN_DIR)/synth_netlist.json \
		$(TOP_MODULE) \
		alu4_plain \
		$(PLAIN_DIR)/alu4


optimized_synth: $(CNF_GEN) scripts/synth_direct.ys
	mkdir -p $(OPT_DIR)
	@$(PYTHON) -c 'assert int("$(N)") >= 2, "N must be at least 2"'
	cp $(TOP_V) $(OPT_DIR)/alu4.v
	sed 's/^chparam -set N 4 /chparam -set N $(N) /' scripts/synth_direct.ys > $(OPT_DIR)/synth.ys

	cd $(OPT_DIR) && \
		$(YOSYS) -q -s synth.ys

	./$(CNF_GEN) \
		--from-aag \
		$(OPT_DIR)/alu4.aag \
		alu4_optimized \
		$(OPT_DIR)/alu4 \
		$(OPT_DIR)/alu4.map


miter: $(MITER_GEN) plain_synth optimized_synth
	mkdir -p $(dir $(SYNTH_MITER))
	./$(MITER_GEN) \
		$(PLAIN_DIR)/alu4.cnf \
		$(PLAIN_DIR)/alu4.iomap \
		$(OPT_DIR)/alu4.cnf \
		$(OPT_DIR)/alu4.iomap \
		$(SYNTH_MITER)

	@echo
	@echo "Miter written to $(SYNTH_MITER)"


verify: plain_synth optimized_synth $(MITER_GEN) $(SOLVER) build/solvers/minisat/minisat
	$(PYTHON) verify.py --width $(N) --plain $(PLAIN_DIR) --optimized $(OPT_DIR) --miter $(SYNTH_MITER)


# ==================================================
# RTL equivalence tests
#
# Example:
#
#   make equiv TEST=alu
#
# Delegates the actual workflow to:
#
#   tests/equivalent/equiv.mk
# ==================================================

equiv: tools
	$(MAKE) \
		-C $(EQUIV_DIR) \
		-f $(EQUIV_MAKEFILE) \
		TEST=$(TEST) \
		run



# ==================================================
# Cleanup
# ==================================================

# Test cleanup preserves tools and saved benchmark reports.
clean-tests:
	rm -rf build/tests build/equivalent build/synthesis
	$(MAKE) -C tests/BMC/counter clean
	$(MAKE) -C tests/BMC/toggle clean

# All generated project outputs, including benchmark reports.
clean:
	rm -rf build
	$(MAKE) -C tests/BMC/counter clean
	$(MAKE) -C tests/BMC/toggle clean
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
	# Remove outputs from the legacy layout too.
	rm -f cnf_gen miter_gen bmc_gen miter_circuit/cex_decoder
	rm -f solvers/backtracking/backtracking_solver solvers/minisat/minisat
	rm -f alu4_miter.cnf
	rm -rf plain_synth optimized_synth
	@if test -d miter_circuit; then rmdir --ignore-fail-on-non-empty miter_circuit; fi


# ==================================================
# Phony targets
# ==================================================

.PHONY: \
	all \
	tools \
	equiv \
	miter \
	verify \
	plain_synth \
	optimized_synth \
	clean \
	clean-tests
