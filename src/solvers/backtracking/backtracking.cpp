#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cnf.hpp"

enum class Value : int8_t {
    Unassigned = -1,
    False = 0,
    True = 1
};

enum class FormulaState {
    Conflict,
    Undecided,
    Satisfied
};

struct SolverStats {
    std::uint64_t nodes = 0;
    std::uint64_t decisions = 0;
    std::uint64_t backtracks = 0;
};

class BacktrackingSolver {
public:
    explicit BacktrackingSolver(const CnfFormula& formula)
        : formula_(formula),
          assignment_(formula.num_vars + 1, Value::Unassigned) {}

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
    std::vector<Value> assignment_;
    SolverStats stats_;

    bool literal_is_true(Lit lit) const {
        const int var = std::abs(lit);
        const Value value = assignment_[var];

        if (value == Value::Unassigned)
            return false;

        const bool variable_value = (value == Value::True);

        return lit > 0
            ? variable_value
            : !variable_value;
    }

    FormulaState evaluate_formula() const {
        bool all_satisfied = true;

        for (const Clause& clause : formula_.clauses) {

            bool clause_satisfied = false;
            bool has_unassigned = false;

            for (Lit lit : clause) {
                const int var = std::abs(lit);

                if (assignment_[var] == Value::Unassigned) {
                    has_unassigned = true;
                    continue;
                }

                if (literal_is_true(lit)) {
                    clause_satisfied = true;
                    break;
                }
            }

            if (!clause_satisfied && !has_unassigned)
                return FormulaState::Conflict;

            if (!clause_satisfied)
                all_satisfied = false;
        }

        return all_satisfied
            ? FormulaState::Satisfied
            : FormulaState::Undecided;
    }

    int choose_unassigned_variable() const {
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
            return false;

        // Try false.
        ++stats_.decisions;

        assignment_[var] = Value::False;

        if (search())
            return true;

        // Try true.
        ++stats_.decisions;

        assignment_[var] = Value::True;

        if (search())
            return true;

        // Neither worked.
        assignment_[var] = Value::Unassigned;
        ++stats_.backtracks;

        return false;
    }
};


static void print_model(
    std::ostream& out,
    const std::vector<Value>& assignment)
{
    out << "v ";

    for (std::size_t var = 1;
         var < assignment.size();
         ++var)
    {
        /*
         * search() may stop once every clause is already satisfied.
         *
         * Any remaining unassigned variables are don't-cares.
         * We output them as false to obtain a complete model.
         */
        const bool value =
            assignment[var] == Value::True;

        out << (value
                    ? static_cast<int>(var)
                    : -static_cast<int>(var))
            << ' ';
    }

    out << "0\n";
}


int main(int argc, char** argv) {

    if (argc != 2 && argc != 4) {
        std::cerr
            << "usage: "
            << argv[0]
            << " <miter.cnf> [--model <model.txt>]\n";

        return 1;
    }

    std::string model_path;

    if (argc == 4) {

        if (std::string(argv[2]) != "--model") {
            std::cerr
                << "error: expected --model <model.txt>\n";

            return 1;
        }

        model_path = argv[3];
    }

    try {

        const std::string cnf_path = argv[1];

        const CnfFormula formula =
            read_dimacs(cnf_path);

        std::cout
            << "c loaded " << cnf_path << '\n';

        std::cout
            << "c variables: "
            << formula.num_vars << '\n';

        std::cout
            << "c clauses:   "
            << formula.clauses.size() << '\n';

        std::cout
            << "c solver: plain recursive backtracking "
            << "(no BCP, no learning)\n";


        BacktrackingSolver solver(formula);

        const bool sat = solver.solve();


        if (sat) {

            std::cout << "s SATISFIABLE\n";

            print_model(
                std::cout,
                solver.assignment()
            );

            if (!model_path.empty()) {

                std::ofstream model_out(model_path);

                if (!model_out) {
                    throw std::runtime_error(
                        "cannot write model file: "
                        + model_path
                    );
                }

                print_model(
                    model_out,
                    solver.assignment()
                );

                std::cout
                    << "c model written to "
                    << model_path << '\n';
            }

        } else {

            std::cout << "s UNSATISFIABLE\n";

            /*
             * Remove any previous SAT model content.
             */
            if (!model_path.empty()) {
                std::ofstream clear_model(
                    model_path,
                    std::ios::trunc
                );
            }
        }


        const SolverStats& stats =
            solver.stats();

        std::cout
            << "c search nodes: "
            << stats.nodes << '\n';

        std::cout
            << "c decisions:    "
            << stats.decisions << '\n';

        std::cout
            << "c backtracks:   "
            << stats.backtracks << '\n';


        // Standard SAT solver convention.
        return sat ? 10 : 20;

    } catch (const std::exception& e) {

        std::cerr
            << "error: "
            << e.what()
            << '\n';

        return 1;
    }
}