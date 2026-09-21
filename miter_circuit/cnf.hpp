#pragma once

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Lit = int;
using Clause = std::vector<Lit>;

struct CnfFormula {
    int num_vars = 0;
    std::vector<Clause> clauses;

    int new_var() {
        return ++num_vars;
    }

    void add_clause(Clause clause) {
        clauses.push_back(std::move(clause));
    }
};

inline CnfFormula read_dimacs(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot open CNF file: " + path);

    CnfFormula cnf;
    bool saw_header = false;
    Clause current_clause;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string first;
        if (!(iss >> first))
            continue;

        if (first == "c")
            continue;

        if (first == "p") {
            std::string format;
            int declared_clauses = 0;
            if (!(iss >> format >> cnf.num_vars >> declared_clauses) || format != "cnf")
                throw std::runtime_error("invalid DIMACS header in: " + path);
            saw_header = true;
            continue;
        }

        // Clause data can legally span lines, so feed the first token back in
        // and keep collecting literals until a terminating 0 is encountered.
        iss.clear();
        iss.str(line);

        int lit = 0;
        while (iss >> lit) {
            if (lit == 0) {
                cnf.add_clause(std::move(current_clause));
                current_clause.clear();
            } else {
                current_clause.push_back(lit);
            }
        }
    }

    if (!saw_header)
        throw std::runtime_error("missing DIMACS header in: " + path);
    if (!current_clause.empty())
        throw std::runtime_error("unterminated DIMACS clause in: " + path);

    for (const auto& clause : cnf.clauses) {
        for (Lit lit : clause) {
            if (std::abs(lit) > cnf.num_vars)
                throw std::runtime_error("literal exceeds DIMACS variable count in: " + path);
        }
    }

    return cnf;
}

inline void write_dimacs(const CnfFormula& cnf,
                         const std::string& path,
                         const std::string& comment = "") {
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot write CNF file: " + path);

    if (!comment.empty())
        out << "c " << comment << '\n';

    out << "p cnf " << cnf.num_vars << ' ' << cnf.clauses.size() << '\n';
    for (const auto& clause : cnf.clauses) {
        for (Lit lit : clause)
            out << lit << ' ';
        out << "0\n";
    }
}
