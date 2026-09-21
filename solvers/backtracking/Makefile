CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

TARGET := backtracking_solver
SOURCE := backtracking.cpp
CNF_HEADER := ../../miter_circuit/cnf.hpp

$(TARGET): $(SOURCE) $(CNF_HEADER)
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCE)

# Run from this directory with:
#   make solve CNF=../../miter_circuit/alu4_miter.cnf
CNF ?= ../../miter_circuit/alu4_miter.cnf

solve: $(TARGET)
	./$(TARGET) $(CNF) || test $$? -eq 20 -o $$? -eq 10

clean:
	rm -f $(TARGET)

.PHONY: solve clean