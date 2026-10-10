#pragma once
// Frontend: ASCII AIGER (.aag) -> CnfBuilder.
//
// AIGER already encodes inversion via odd/even literals, so this frontend
// gets "NOT is free" for the same reason the JSON frontend has to work for
// it: a literal's sign IS the inversion, never a separate gate.
//
// Port names come from a Yosys `-map` sidecar file (lines "i<idx> <name>"
// and "o<idx> <name>"), NOT from guessing based on literal order. If no
// map file is given, inputs/outputs are named generically (pi0, pi1, ...,
// po0, po1, ...) so the tool still runs, but callers should always supply
// a map file for anything beyond a quick smoke test.

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tseitin.hpp"

struct AagSymbol {
    std::string port;
    int bit;
};

// Parses a Yosys `-map` file into {i-index -> symbol}, {o-index -> symbol}.
// Real format (confirmed from actual Yosys 0.67 output, not the docs):
//   input <pi_index> <bit_index> <port_name>
//   output <po_index> <bit_index> <port_name>
// <pi_index>/<po_index> is the 0-based position in the AAG's PI/PO list;
// <bit_index> is the bit position within that (possibly multi-bit) port.
inline void parse_aiger_map(const std::string& path,
                             std::map<int, AagSymbol>& in_syms,
                             std::map<int, AagSymbol>& out_syms) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open map file: " + path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string kind;
        int idx, bit;
        std::string port;
        if (!(iss >> kind >> idx >> bit >> port)) continue; // skip malformed/other lines
        if (kind == "input") in_syms[idx] = {port, bit};
        else if (kind == "output") out_syms[idx] = {port, bit};
        // ignore latch/box/other kinds -- not relevant to combinational PI/PO naming
    }
}

// lit -> CnfBuilder literal, given a table of already-resolved AIGER
// variable indices (aiger var = lit/2). AIGER literal 0/1 are constants.
inline int resolve_aiger_lit(int aiger_lit, const std::vector<int>& var_to_cnf_lit,
                              int true_lit, int false_lit) {
    if (aiger_lit == 0) return false_lit;
    if (aiger_lit == 1) return true_lit;
    int var = aiger_lit / 2;
    bool inverted = aiger_lit & 1;
    int base = var_to_cnf_lit[var];
    return inverted ? -base : base;
}

inline void run_aag_frontend(const std::string& aag_path, const std::string& map_path,
                              CnfBuilder& cb) {
    std::ifstream f(aag_path);
    if (!f) throw std::runtime_error("cannot open aag file: " + aag_path);

    std::string magic;
    int M, I, L, O, A;
    f >> magic >> M >> I >> L >> O >> A;
    if (magic != "aag") throw std::runtime_error("not an ASCII AIGER file: " + aag_path);
    if (L != 0) throw std::runtime_error("sequential AIGER (latches) not supported yet");

    // var_to_cnf_lit[aiger_var] = CNF literal for that AIGER variable's
    // "true sense" (i.e. as if referenced with an even/non-inverted literal).
    std::vector<int> var_to_cnf_lit(M + 1, 0);

    std::vector<int> input_lits(I);
    for (int i = 0; i < I; i++) f >> input_lits[i];
    // combinational only: no latch lines to skip (L == 0, checked above)
    std::vector<int> output_lits(O);
    for (int i = 0; i < O; i++) f >> output_lits[i];

    // Primary inputs: fresh variable each, in file order.
    for (int i = 0; i < I; i++) {
        int var = input_lits[i] / 2;
        var_to_cnf_lit[var] = cb.new_var();
    }

    // AND gates: file order is already topological (AIGER invariant: every
    // AND's operands reference strictly smaller variable indices), so a
    // single linear pass suffices -- no recursion needed.
    for (int i = 0; i < A; i++) {
        int lhs, rhs0, rhs1;
        f >> lhs >> rhs0 >> rhs1;
        int x = resolve_aiger_lit(rhs0, var_to_cnf_lit, cb.true_lit(), cb.false_lit());
        int y = resolve_aiger_lit(rhs1, var_to_cnf_lit, cb.true_lit(), cb.false_lit());
        int z = cb.and2(x, y);
        var_to_cnf_lit[lhs / 2] = z;
    }

    // Names, if a map file was supplied.
    std::map<int, AagSymbol> in_syms, out_syms;
    if (!map_path.empty()) parse_aiger_map(map_path, in_syms, out_syms);

    for (int i = 0; i < I; i++) {
        int lit = resolve_aiger_lit(input_lits[i], var_to_cnf_lit, cb.true_lit(), cb.false_lit());
        std::string port = "pi" + std::to_string(i);
        int bit = 0;
        auto it = in_syms.find(i);
        if (it != in_syms.end()) { port = it->second.port; bit = it->second.bit; }
        cb.add_io('i', port, bit, lit);
    }
    for (int i = 0; i < O; i++) {
        int lit = resolve_aiger_lit(output_lits[i], var_to_cnf_lit, cb.true_lit(), cb.false_lit());
        std::string port = "po" + std::to_string(i);
        int bit = 0;
        auto it = out_syms.find(i);
        if (it != out_syms.end()) { port = it->second.port; bit = it->second.bit; }
        cb.add_io('o', port, bit, lit);
    }
}