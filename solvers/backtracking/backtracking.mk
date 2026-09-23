CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

TARGET := backtracking_solver
SOURCE := backtracking.cpp

CNF_HEADER := ../../miter_circuit/cnf.hpp

all: $(TARGET)

$(TARGET): $(SOURCE) $(CNF_HEADER)
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCE)

clean:
	rm -f $(TARGET)

.PHONY: all clean