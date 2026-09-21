CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
YOSYS ?= yosys
PYTHON ?= python3

TOP_V ?= alu4.v
TOP_MODULE ?= alu4
MITER_CNF ?= alu4_miter.cnf

# Default goal: build the final equivalence-checking CNF.
all: miter

# Circuit -> CNF converter.
cnf_gen: main.cpp tseitin.hpp aag_frontend.hpp json_frontend.hpp json.hpp
	$(CXX) $(CXXFLAGS) -o $@ main.cpp

# Two CNFs + two I/O maps -> one miter CNF.
miter_gen: miter_circuit/miter.cpp miter_circuit/cnf.hpp
	$(CXX) $(CXXFLAGS) -o $@ miter_circuit/miter.cpp

# Unoptimized route.
plain_synth: cnf_gen
	mkdir -p plain_synth
	cp $(TOP_V) plain_synth/
	cd plain_synth && $(YOSYS) -q -s ../synth_netlist.ys
	./cnf_gen --from-json \
		plain_synth/synth_netlist.json \
		$(TOP_MODULE) \
		alu4_plain \
		plain_synth/alu4

# Optimized AIG route.
optimized_synth: cnf_gen
	mkdir -p optimized_synth
	cp $(TOP_V) optimized_synth/
	cd optimized_synth && $(YOSYS) -q -s ../synth_direct.ys
	./cnf_gen --from-aag \
		optimized_synth/alu4.aag \
		alu4_optimized \
		optimized_synth/alu4 \
		optimized_synth/alu4.map



miter: miter_gen plain_synth optimized_synth
	./miter_gen \
		plain_synth/alu4.cnf \
		plain_synth/alu4.iomap \
		optimized_synth/alu4.cnf \
		optimized_synth/alu4.iomap \
		$(MITER_CNF)
	@echo "miter written to $(MITER_CNF)"

# Existing exhaustive regression test.
verify: plain_synth optimized_synth
	$(PYTHON) verify.py

clean:
	rm -f cnf_gen miter_gen $(MITER_CNF)
	rm -rf plain_synth optimized_synth

.PHONY: all plain_synth optimized_synth miter verify clean