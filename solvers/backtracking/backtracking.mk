CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

TARGET := ../../build/solvers/backtracking/backtracking_solver
SOURCE := ../../src/solvers/backtracking/backtracking.cpp

CNF_HEADER := ../../src/common/cnf.hpp

all: $(TARGET)

$(TARGET): $(SOURCE) $(CNF_HEADER)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -I../../src/common $(CXXFLAGS) -o $@ $(SOURCE)

clean:
	rm -f $(TARGET)

.PHONY: all clean