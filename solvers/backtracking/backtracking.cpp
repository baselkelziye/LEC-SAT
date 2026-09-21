#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Reuse the same CNF representation / DIMACS parser used by the miter.
// Directory layout expected:
//   miter_circuit/cnf.hpp
//   solvers/backtracking/backtracking.cpp
#include "../../miter_circuit/cnf.hpp"

enum class Value : int8_t {
    Unassigned = -1,
    False = 0,
    True = 1
};

enum class FormulaState {
    Conflict,   // At least one clause is false under the current partial assignment.
    Undecided,  // No conflict, but at least one clause still needs an assignment.
    Satisfied   // Every clause already has a true literal.
};

struct SolverStats {
    std::uint64_t nodes = 0;
    std::uint64_t decisions = 0;
    std::uint64_t backtracks = 0;
};

class BacktrackingSolver {
public:
    explicit BacktrackingSolver(const CnfFormula& formula)
        : formula_(formula), assignment_(formula.num_vars + 1, Value::Unassigned) {}

    bool solve() {
        return search();
    }

    const std::vector<Value>& assignment() const {
        return assignment_;
    }

    const SolverStats& stats() const {
        return stats_;
    }

private:
    const CnfFormula& formula_;
    std::vector<Value> assignment_; // Index 0 is unused; DIMACS variables start at 1.
    SolverStats stats_;

    bool literal_is_true(Lit lit) const {
        const int var = std::abs(lit);
        const Value value = assignment_[var];

        if (value == Value::Unassigned)
            return false;

        const bool variable_is_true = (value == Value::True);
        return lit > 0 ? variable_is_true : !variable_is_true;
    }

    FormulaState evaluate_formula() const {
        bool all_clauses_satisfied = true;

        for (const Clause& clause : formula_.clauses) {
            bool clause_satisfied = false;
            bool clause_has_unassigned = false;

            for (Lit lit : clause) {
                const int var = std::abs(lit);

                if (assignment_[var] == Value::Unassigned) {
                    clause_has_unassigned = true;
                    continue;
                }

                if (literal_is_true(lit)) {
                    clause_satisfied = true;
                    break;
                }
            }

            // A clause is false only when every literal has been assigned
            // and none of those literals is true.
            if (!clause_satisfied && !clause_has_unassigned)
                return FormulaState::Conflict;

            if (!clause_satisfied)
                all_clauses_satisfied = false;
        }

        return all_clauses_satisfied
            ? FormulaState::Satisfied
            : FormulaState::Undecided;
    }

    int choose_unassigned_variable() const {
        // Intentionally naive: choose the first unassigned DIMACS variable.
        // Later DPLL/CDCL versions can replace only this policy.
        for (int var = 1; var <= formula_.num_vars; ++var) {
            if (assignment_[var] == Value::Unassigned)
                return var;
        }
        return 0;
    }

    bool search() {
        ++stats_.nodes;

        const FormulaState state = evaluate_formula();

        if (state == FormulaState::Conflict)
            return false;

        if (state == FormulaState::Satisfied)
            return true;

        const int var = choose_unassigned_variable();
        if (var == 0)
            return false; // Defensive: all vars assigned but formula not satisfied.

        // Branch 1: var = false.
        ++stats_.decisions;
        assignment_[var] = Value::False;
        if (search())
            return true;

        // Branch 2: var = true.
        ++stats_.decisions;
        assignment_[var] = Value::True;
        if (search())
            return true;

        // Neither value worked: undo the decision and backtrack.
        assignment_[var] = Value::Unassigned;
        ++stats_.backtracks;
        return false;
    }
};

static void print_model(const std::vector<Value>& assignment) {
    std::cout << "v ";
    for (std::size_t var = 1; var < assignment.size(); ++var) {
        // If search stopped early because every clause was already satisfied,
        // remaining unassigned variables are don't-cares. Print them as false
        // to produce a complete model.
        const bool value = assignment[var] == Value::True;
        std::cout << (value ? static_cast<int>(var) : -static_cast<int>(var)) << ' ';
    }
    std::cout << "0\n";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <miter.cnf>\n";
        std::cerr << "example: " << argv[0] << " miter_circuit/alu4_miter.cnf\n";
        return 1;
    }

    try {
        const std::string cnf_path = argv[1];
        const CnfFormula formula = read_dimacs(cnf_path);

        std::cout << "c loaded " << cnf_path << '\n';
        std::cout << "c variables: " << formula.num_vars << '\n';
        std::cout << "c clauses:   " << formula.clauses.size() << '\n';
        std::cout << "c solver: plain recursive backtracking (no BCP, no learning)\n";

        BacktrackingSolver solver(formula);
        const bool sat = solver.solve();

        if (sat) {
            std::cout << "s SATISFIABLE\n";
            print_model(solver.assignment());
        } else {
            std::cout << "s UNSATISFIABLE\n";
        }

        const SolverStats& stats = solver.stats();
        std::cout << "c search nodes: " << stats.nodes << '\n';
        std::cout << "c decisions:    " << stats.decisions << '\n';
        std::cout << "c backtracks:   " << stats.backtracks << '\n';

        return sat ? 10 : 20; // Conventional SAT-solver exit codes.
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}