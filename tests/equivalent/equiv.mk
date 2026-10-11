ROOT := $(abspath ../..)

YOSYS ?= yosys

TEST ?= alu
N ?= 4
EQUIV_BUILD_ROOT ?= $(ROOT)/build/equivalent

# ==================================================
# Test sources
# ==================================================

CASE_DIR := $(CURDIR)/$(TEST)

LHS_V := $(CASE_DIR)/lhs.v
RHS_V := $(CASE_DIR)/rhs.v
EXPECTED_FILE := $(CASE_DIR)/expected.txt


# ==================================================
# Generated files
# ==================================================

BUILD := $(EQUIV_BUILD_ROOT)/$(TEST)

LHS_BUILD := $(BUILD)/lhs
RHS_BUILD := $(BUILD)/rhs

MITER := $(BUILD)/miter.cnf
MODEL := $(BUILD)/model.txt
CEX := $(BUILD)/cex.txt


# ==================================================
# Tools
# ==================================================

SYNTH_SCRIPT := $(ROOT)/eqy_test.ys

CNF_GEN := $(ROOT)/build/combinational/cnf_gen
MITER_GEN := $(ROOT)/build/equivalence/miter_gen
PYTHON ?= python3
SOLVER := $(PYTHON) $(ROOT)/solver.py
CEX_DECODER := $(ROOT)/build/equivalence/cex_decoder


# ==================================================
# Main flow
# ==================================================

run: solve


# ==================================================
# Sanity checks
# ==================================================

check:
	@$(PYTHON) -c 'assert int("$(N)") >= 2, "N must be at least 2"'
	@mkdir -p $(BUILD)
	@rm -f $(MODEL) $(CEX)
	@case "$$(cat "$(EXPECTED_FILE)")" in SAT|UNSAT) ;; \
		*) echo "Expected SAT or UNSAT in $(EXPECTED_FILE)"; exit 1 ;; esac
	@test -f "$(LHS_V)" || { \
		echo "Missing $(LHS_V)"; \
		exit 1; \
	}

	@test -f "$(RHS_V)" || { \
		echo "Missing $(RHS_V)"; \
		exit 1; \
	}


# ==================================================
# LHS synthesis
# ==================================================

lhs: check
	@echo
	@echo "=== LHS: $(TEST) ==="

	mkdir -p $(LHS_BUILD)

	cp $(LHS_V) $(LHS_BUILD)/design.v

	sed '/^read_verilog/a chparam -set N $(N) lhs' $(SYNTH_SCRIPT) > $(LHS_BUILD)/synth.ys

	cd $(LHS_BUILD) && \
		$(YOSYS) -q -s synth.ys

	$(CNF_GEN) \
		--from-aag \
		$(LHS_BUILD)/design.aag \
		lhs \
		$(LHS_BUILD)/design \
		$(LHS_BUILD)/design.map


# ==================================================
# RHS synthesis
# ==================================================

rhs: check
	@echo
	@echo "=== RHS: $(TEST) ==="

	mkdir -p $(RHS_BUILD)

	cp $(RHS_V) $(RHS_BUILD)/design.v

	sed '/^read_verilog/a chparam -set N $(N) rhs' $(SYNTH_SCRIPT) > $(RHS_BUILD)/synth.ys

	cd $(RHS_BUILD) && \
		$(YOSYS) -q -s synth.ys

	$(CNF_GEN) \
		--from-aag \
		$(RHS_BUILD)/design.aag \
		rhs \
		$(RHS_BUILD)/design \
		$(RHS_BUILD)/design.map


# ==================================================
# Miter
# ==================================================

miter: lhs rhs
	@echo
	@echo "=== Building miter ==="

	$(MITER_GEN) \
		$(LHS_BUILD)/design.cnf \
		$(LHS_BUILD)/design.iomap \
		$(RHS_BUILD)/design.cnf \
		$(RHS_BUILD)/design.iomap \
		$(MITER)

	@echo "miter: $(MITER)"


# ==================================================
# SAT solver + CEX
# ==================================================

solve: miter
	@echo
	@echo "=== Solving $(TEST) ==="

	@rm -f $(MODEL) $(CEX); \
	$(SOLVER) $(MITER) --model $(MODEL); \
	rc=$$?; \
	if [ $$rc -eq 10 ]; then \
		actual=SAT; \
		echo; \
		echo "Result: SAT - designs are NOT equivalent"; \
		echo; \
		echo "=== Counterexample ==="; \
		test -f "$(MODEL)" || { \
			echo "ERROR: SAT returned but model was not created"; \
			exit 1; \
		}; \
		$(CEX_DECODER) \
			$(MODEL) \
			$(LHS_BUILD)/design.cnf \
			$(LHS_BUILD)/design.iomap \
			$(RHS_BUILD)/design.cnf \
			$(RHS_BUILD)/design.iomap \
			> $(CEX) || exit 1; \
		cat $(CEX); \
		echo; \
		echo "Raw model: $(MODEL)"; \
		echo "CEX file:  $(CEX)"; \
	elif [ $$rc -eq 20 ]; then \
		actual=UNSAT; \
		echo; \
		echo "Result: UNSAT - designs are equivalent"; \
		rm -f $(MODEL) $(CEX); \
	else \
		echo "Solver failed with exit code $$rc"; \
		exit 1; \
	fi; \
	expected=$$(cat "$(EXPECTED_FILE)"); \
	if [ "$$actual" != "$$expected" ]; then \
		echo "FAIL $(TEST): expected $$expected, got $$actual"; \
		exit 1; \
	fi; \
	echo "PASS $(TEST): expected $$expected, got $$actual"


# ==================================================
# Cleanup
# ==================================================

clean:
	rm -rf $(ROOT)/build/equivalent


.PHONY: \
	run \
	check \
	lhs \
	rhs \
	miter \
	solve \
	clean
