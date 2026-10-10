#include "cnf.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct SignalKey {
    std::string port;
    int bit = 0;

    bool operator<(const SignalKey& other) const {
        if (port != other.port)
            return port < other.port;
        return bit < other.bit;
    }
};

using SignalMap = std::map<SignalKey, Lit>;

struct CircuitCnf {
    CnfFormula formula;
    SignalMap inputs;
    SignalMap outputs;
};

static std::string signal_name(const SignalKey& s) {
    return s.port + "[" + std::to_string(s.bit) + "]";
}

static SignalMap read_iomap_direction(const std::string& path, char wanted_direction) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot open iomap file: " + path);

    SignalMap result;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty())
            continue;

        std::istringstream iss(line);
        char direction = 0;

        // Comments begin with 'c'.
        if (!(iss >> direction) || direction == 'c')
            continue;

        std::string port;
        int bit = 0;
        int var = 0;
        char polarity = 0;

        if (!(iss >> port >> bit >> var >> polarity))
            throw std::runtime_error("invalid iomap line in: " + path + ": " + line);

        if (direction != 'i' && direction != 'o')
            throw std::runtime_error("invalid iomap direction in: " + path);
        if (polarity != '+' && polarity != '-')
            throw std::runtime_error("invalid iomap polarity in: " + path);
        if (var <= 0)
            throw std::runtime_error("invalid iomap variable in: " + path);

        if (direction != wanted_direction)
            continue;

        SignalKey key{port, bit};
        Lit lit = (polarity == '+') ? var : -var;

        if (!result.emplace(key, lit).second)
            throw std::runtime_error("duplicate iomap signal: " + signal_name(key));
    }

    return result;
}

static CircuitCnf load_circuit(const std::string& cnf_path,
                               const std::string& iomap_path) {
    CircuitCnf circuit;
    circuit.formula = read_dimacs(cnf_path);
    circuit.inputs = read_iomap_direction(iomap_path, 'i');
    circuit.outputs = read_iomap_direction(iomap_path, 'o');

    auto validate_map = [&](const SignalMap& signals) {
        for (const auto& [key, lit] : signals) {
            if (std::abs(lit) > circuit.formula.num_vars) {
                throw std::runtime_error(
                    "iomap signal " + signal_name(key) +
                    " refers to a CNF variable outside the declared range");
            }
        }
    };

    validate_map(circuit.inputs);
    validate_map(circuit.outputs);
    return circuit;
}

static Lit shift_lit(Lit lit, int offset) {
    return lit > 0 ? lit + offset : lit - offset;
}

// Encode logical equality a <-> b.
// These are literals, not necessarily positive variables.
static void add_equal(CnfFormula& cnf, Lit a, Lit b) {
    cnf.add_clause({-a, b});
    cnf.add_clause({a, -b});
}

// Create a fresh variable z and encode z <-> (a XOR b).
static Lit add_xor(CnfFormula& cnf, Lit a, Lit b) {
    Lit z = cnf.new_var();

    cnf.add_clause({-z,  a,  b});
    cnf.add_clause({-z, -a, -b});
    cnf.add_clause({ z,  a, -b});
    cnf.add_clause({ z, -a,  b});

    return z;
}

static void require_same_interface(const SignalMap& lhs,
                                   const SignalMap& rhs,
                                   const std::string& kind) {
    if (lhs.size() != rhs.size()) {
        throw std::runtime_error(
            kind + " interface size mismatch: lhs has " +
            std::to_string(lhs.size()) + ", rhs has " +
            std::to_string(rhs.size()));
    }

    for (const auto& [key, unused] : lhs) {
        (void)unused;
        if (rhs.find(key) == rhs.end())
            throw std::runtime_error(
                kind + " missing on rhs: " + signal_name(key));
    }
}

static CnfFormula build_miter(const CircuitCnf& lhs,
                              const CircuitCnf& rhs) {
    require_same_interface(lhs.inputs, rhs.inputs, "input");
    require_same_interface(lhs.outputs, rhs.outputs, "output");

    if (lhs.outputs.empty())
        throw std::runtime_error("cannot build a miter for a circuit with no outputs");

    CnfFormula miter;

    // Step 1: copy the left circuit exactly as-is.
    miter.num_vars = lhs.formula.num_vars;
    for (const Clause& clause : lhs.formula.clauses)
        miter.add_clause(clause);

    // Step 2: put the right circuit into a disjoint variable namespace.
    // If lhs uses 1..N, rhs variable v becomes v+N (with sign preserved).
    const int rhs_offset = lhs.formula.num_vars;
    for (const Clause& clause : rhs.formula.clauses) {
        Clause shifted;
        shifted.reserve(clause.size());
        for (Lit lit : clause)
            shifted.push_back(shift_lit(lit, rhs_offset));
        miter.add_clause(std::move(shifted));
    }
    miter.num_vars += rhs.formula.num_vars;

    // Step 3: corresponding primary inputs must carry the same logical value.
    for (const auto& [key, lhs_lit] : lhs.inputs) {
        Lit rhs_lit = shift_lit(rhs.inputs.at(key), rhs_offset);
        add_equal(miter, lhs_lit, rhs_lit);
    }

    // Step 4: one XOR per corresponding output bit.
    // diff_i = 1 exactly when that output bit differs.
    std::vector<Lit> differences;
    differences.reserve(lhs.outputs.size());

    for (const auto& [key, lhs_lit] : lhs.outputs) {
        Lit rhs_lit = shift_lit(rhs.outputs.at(key), rhs_offset);
        differences.push_back(add_xor(miter, lhs_lit, rhs_lit));
    }

    // Step 5: ask SAT to find a counterexample.
    // At least one diff_i must be true.
    // SAT   => such an input exists => circuits are NOT equivalent.
    // UNSAT => no such input exists => circuits ARE equivalent.
    miter.add_clause(std::move(differences));

    return miter;
}

static void usage(const char* prog) {
    std::cerr
        << "usage: " << prog
        << " <lhs.cnf> <lhs.iomap> <rhs.cnf> <rhs.iomap> <out.cnf>\n";
}

int main(int argc, char** argv) {
    if (argc != 6) {
        usage(argv[0]);
        return 1;
    }

    try {
        CircuitCnf lhs = load_circuit(argv[1], argv[2]);
        CircuitCnf rhs = load_circuit(argv[3], argv[4]);

        CnfFormula miter = build_miter(lhs, rhs);
        write_dimacs(miter, argv[5], "equivalence miter; SAT means output mismatch exists");

        std::cerr
            << "wrote " << argv[5] << " ("
            << miter.num_vars << " vars, "
            << miter.clauses.size() << " clauses)\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}