ROOT := $(abspath ../..)

YOSYS ?= yosys

TEST ?= alu

# ==================================================
# Test sources
# ==================================================

CASE_DIR := $(CURDIR)/$(TEST)

LHS_V := $(CASE_DIR)/lhs.v
RHS_V := $(CASE_DIR)/rhs.v


# ==================================================
# Generated files
# ==================================================

BUILD := $(ROOT)/build/equivalent/$(TEST)

LHS_BUILD := $(BUILD)/lhs
RHS_BUILD := $(BUILD)/rhs

MITER := $(BUILD)/miter.cnf
MODEL := $(BUILD)/model.txt
CEX := $(BUILD)/cex.txt


# ==================================================
# Tools
# ==================================================

SYNTH_SCRIPT := $(ROOT)/eqy_test.ys

CNF_GEN := $(ROOT)/cnf_gen
MITER_GEN := $(ROOT)/miter_gen
SOLVER := $(ROOT)/solvers/backtracking/backtracking_solver
CEX_DECODER := $(ROOT)/miter_circuit/cex_decoder


# Automatically discover test directories.
TESTS := $(sort $(notdir $(wildcard */)))


# ==================================================
# Main flow
# ==================================================

run: solve


# ==================================================
# Sanity checks
# ==================================================

check:
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

	cd $(LHS_BUILD) && \
		$(YOSYS) -q -s $(SYNTH_SCRIPT)

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

	cd $(RHS_BUILD) && \
		$(YOSYS) -q -s $(SYNTH_SCRIPT)

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
			| tee $(CEX); \
		echo; \
		echo "Raw model: $(MODEL)"; \
		echo "CEX file:  $(CEX)"; \
	elif [ $$rc -eq 20 ]; then \
		echo; \
		echo "Result: UNSAT - designs are equivalent"; \
		rm -f $(MODEL) $(CEX); \
	else \
		echo "Solver failed with exit code $$rc"; \
		exit $$rc; \
	fi


# ==================================================
# Regression
# ==================================================

regression:
	@set -e; \
	for test in $(TESTS); do \
		echo; \
		echo "========================================"; \
		echo "Equivalence test: $$test"; \
		echo "========================================"; \
		$(MAKE) -f equiv.mk run TEST=$$test; \
	done


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
	regression \
	clean