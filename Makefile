CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

YOSYS ?= yosys
PYTHON ?= python3


# ==================================================
# Global configuration
# ==================================================

TOP_V ?= alu4.v
TOP_MODULE ?= alu4
MITER_CNF ?= alu4_miter.cnf

TEST ?= alu


# ==================================================
# Tool paths
# ==================================================

CNF_GEN := cnf_gen
MITER_GEN := miter_gen

SOLVER_DIR := solvers/backtracking
SOLVER_MAKEFILE := backtracking.mk
SOLVER_SRC := $(SOLVER_DIR)/backtracking.cpp
SOLVER := $(SOLVER_DIR)/backtracking_solver

CEX_DECODER := miter_circuit/cex_decoder

EQUIV_DIR := tests/equivalent
EQUIV_MAKEFILE := equiv.mk


# ==================================================
# Default
#
# Just build the project tools.
# ==================================================

all: tools


tools: $(CNF_GEN) $(MITER_GEN) $(SOLVER) $(CEX_DECODER)


# ==================================================
# Tools
# ==================================================

$(CNF_GEN): \
	main.cpp \
	tseitin.hpp \
	aag_frontend.hpp \
	json_frontend.hpp \
	json.hpp
	$(CXX) $(CXXFLAGS) -o $@ main.cpp


$(MITER_GEN): \
	miter_circuit/miter.cpp \
	miter_circuit/cnf.hpp
	$(CXX) $(CXXFLAGS) -o $@ miter_circuit/miter.cpp


$(CEX_DECODER): \
	miter_circuit/cex.cpp \
	miter_circuit/cnf.hpp
	$(CXX) $(CXXFLAGS) -o $@ miter_circuit/cex.cpp


$(SOLVER): \
	$(SOLVER_SRC) \
	$(SOLVER_DIR)/$(SOLVER_MAKEFILE) \
	miter_circuit/cnf.hpp
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

plain_synth: $(CNF_GEN)
	mkdir -p plain_synth
	cp $(TOP_V) plain_synth/

	cd plain_synth && \
		$(YOSYS) -q -s ../synth_netlist.ys

	./$(CNF_GEN) \
		--from-json \
		plain_synth/synth_netlist.json \
		$(TOP_MODULE) \
		alu4_plain \
		plain_synth/alu4


optimized_synth: $(CNF_GEN)
	mkdir -p optimized_synth
	cp $(TOP_V) optimized_synth/

	cd optimized_synth && \
		$(YOSYS) -q -s ../synth_direct.ys

	./$(CNF_GEN) \
		--from-aag \
		optimized_synth/alu4.aag \
		alu4_optimized \
		optimized_synth/alu4 \
		optimized_synth/alu4.map


miter: $(MITER_GEN) plain_synth optimized_synth
	./$(MITER_GEN) \
		plain_synth/alu4.cnf \
		plain_synth/alu4.iomap \
		optimized_synth/alu4.cnf \
		optimized_synth/alu4.iomap \
		$(MITER_CNF)

	@echo
	@echo "Miter written to $(MITER_CNF)"


verify: plain_synth optimized_synth
	$(PYTHON) verify.py


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


equiv-regression: tools
	$(MAKE) \
		-C $(EQUIV_DIR) \
		-f $(EQUIV_MAKEFILE) \
		regression


# ==================================================
# Cleanup
# ==================================================

clean-tests:
	rm -rf build


clean:
	rm -f $(CNF_GEN)
	rm -f $(MITER_GEN)
	rm -f $(CEX_DECODER)
	rm -f $(MITER_CNF)

	rm -rf plain_synth
	rm -rf optimized_synth
	rm -rf build

	$(MAKE) \
		-C $(SOLVER_DIR) \
		-f $(SOLVER_MAKEFILE) \
		clean


# ==================================================
# Phony targets
# ==================================================

.PHONY: \
	all \
	tools \
	equiv \
	equiv-regression \
	miter \
	verify \
	plain_synth \
	optimized_synth \
	clean \
	clean-tests