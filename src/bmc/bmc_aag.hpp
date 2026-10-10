#pragma once
// Parse a reusable sequential circuit before allocating any time-frame variables.
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "tseitin.hpp"

struct BmcLatch { int q, d, init; }; // init == q means unconstrained
struct BmcAnd { int lhs, a, b; };
struct BmcAag {
    int maxvar = 0;
    std::vector<int> inputs, outputs, bad;
    std::vector<BmcLatch> latches;
    std::vector<BmcAnd> gates;
    std::vector<std::string> input_names, latch_names, output_names;
};

inline std::vector<int> bmc_numbers(const std::string& line) {
    std::istringstream in(line);
    std::vector<int> result;
    long long n;
    while (in >> n) {
        if (n < 0 || n > std::numeric_limits<int>::max() / 2)
            throw std::runtime_error("AAG number out of range");
        result.push_back(static_cast<int>(n));
    }
    if (!in.eof()) throw std::runtime_error("invalid AAG numeric record");
    return result;
}

inline BmcAag read_bmc_aag(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open AAG: " + path);
    std::string line;
    if (!std::getline(in, line) || line.substr(0, 4) != "aag ")
        throw std::runtime_error("expected ASCII AIGER header");
    auto h = bmc_numbers(line.substr(4));
    if (h.size() < 5 || h.size() > 9) throw std::runtime_error("invalid AAG header");
    h.resize(9, 0);
    if (h[6] || h[7] || h[8])
        throw std::runtime_error("BMC does not yet support constraints, justice, or fairness sections");
    BmcAag m;
    m.maxvar = h[0];
    if (static_cast<long long>(h[1]) + h[2] + h[4] > m.maxvar)
        throw std::runtime_error("AAG definitions exceed maximum variable index");
    auto record = [&](size_t lo, size_t hi) {
        if (!std::getline(in, line)) throw std::runtime_error("truncated AAG");
        auto r = bmc_numbers(line);
        if (r.size() < lo || r.size() > hi) throw std::runtime_error("invalid AAG record length");
        return r;
    };
    for (int i = 0; i < h[1]; ++i) m.inputs.push_back(record(1, 1)[0]);
    for (int i = 0; i < h[2]; ++i) {
        auto r = record(2, 3);
        int init = r.size() == 3 ? r[2] : 0;
        if (init != 0 && init != 1 && init != r[0])
            throw std::runtime_error("unsupported latch initialization");
        m.latches.push_back({r[0], r[1], init});
    }
    for (int i = 0; i < h[3]; ++i) m.outputs.push_back(record(1, 1)[0]);
    for (int i = 0; i < h[5]; ++i) m.bad.push_back(record(1, 1)[0]);
    for (int i = 0; i < h[4]; ++i) {
        auto r = record(3, 3);
        m.gates.push_back({r[0], r[1], r[2]});
    }
    auto names = [](size_t n, const std::string& prefix) {
        std::vector<std::string> v;
        for (size_t i = 0; i < n; ++i) v.push_back(prefix + std::to_string(i));
        return v;
    };
    m.input_names = names(m.inputs.size(), "input");
    m.latch_names = names(m.latches.size(), "state");
    m.output_names = names(m.outputs.size(), "output");
    while (std::getline(in, line)) {
        if (line == "c" || line == "c\r") break;
        if (line.empty()) continue;
        char kind = line[0];
        std::istringstream symbol(line.substr(1));
        size_t index;
        if (!(symbol >> index)) throw std::runtime_error("invalid AAG symbol");
        std::string name;
        std::getline(symbol >> std::ws, name);
        auto* v = kind == 'i' ? &m.input_names : kind == 'l' ? &m.latch_names :
                  kind == 'o' ? &m.output_names : nullptr;
        if (v) {
            if (index >= v->size() || name.empty()) throw std::runtime_error("invalid AAG symbol index/name");
            (*v)[index] = name;
        } else if (kind != 'b' || index >= m.bad.size()) {
            throw std::runtime_error("unsupported AAG symbol");
        }
    }
    // Validate definitions, references and combinational acyclicity. Registers
    // terminate traversal: their next-state expressions are not same-frame edges.
    std::vector<int> driver(m.maxvar + 1, -2);
    auto define = [&](int lit, int gate) {
        if (!lit || (lit & 1) || lit / 2 > m.maxvar || driver[lit / 2] != -2)
            throw std::runtime_error("invalid or duplicate AAG definition");
        driver[lit / 2] = gate;
    };
    for (int x : m.inputs) define(x, -1);
    for (auto l : m.latches) define(l.q, -1);
    for (size_t i = 0; i < m.gates.size(); ++i) define(m.gates[i].lhs, static_cast<int>(i));
    auto check = [&](int lit) {
        if (lit / 2 > m.maxvar || (lit > 1 && driver[lit / 2] == -2))
            throw std::runtime_error("undefined AAG literal");
    };
    for (int x : m.outputs) check(x);
    for (int x : m.bad) check(x);
    for (auto l : m.latches) check(l.d);
    for (auto g : m.gates) { check(g.a); check(g.b); }
    std::vector<int> color(m.gates.size(), 0);
    std::vector<BmcAnd> ordered;
    std::function<void(int)> visit = [&](int i) {
        if (color[i] == 1) throw std::runtime_error("combinational cycle in AAG");
        if (color[i] == 2) return;
        color[i] = 1;
        auto g = m.gates[i];
        for (int lit : {g.a, g.b})
            if (lit > 1 && driver[lit / 2] >= 0) visit(driver[lit / 2]);
        color[i] = 2;
        ordered.push_back(g);
    };
    for (size_t i = 0; i < m.gates.size(); ++i) visit(static_cast<int>(i));
    m.gates = std::move(ordered);
    return m;
}

// One independent exact-depth query. Property is an AIGER literal, not DIMACS.
inline void encode_bmc(const BmcAag& m, int depth, int property,
                       CnfBuilder& cb, std::ostream& map, std::ostream* report = nullptr) {
    std::vector<int> previous;
    auto equal = [&](int a, int b) {
        cb.add_clause({-a, b});
        cb.add_clause({a, -b});
    };
    for (int t = 0; t <= depth; ++t) {
        if (report) *report << "Frame " << t << " (signed DIMACS literals)\n";
        std::vector<int> frame(m.maxvar + 1, 0);
        auto resolve = [&](int lit) {
            if (lit == 0) return cb.false_lit();
            if (lit == 1) return cb.true_lit();
            int base = frame.at(lit / 2);
            if (!base) throw std::runtime_error("unresolved BMC literal");
            return lit & 1 ? -base : base;
        };
        auto entry = [&](char kind, size_t i, int lit, const std::string& name) {
            map << t << '\t' << kind << '\t' << i << '\t' << lit << '\t' << name << '\n';
        };
        for (size_t i = 0; i < m.latches.size(); ++i) {
            auto l = m.latches[i];
            int q = frame[l.q / 2] = cb.new_var();
            if (t == 0 && l.init != l.q) cb.add_clause({l.init == 1 ? q : -q});
            if (t > 0) equal(q, previous[i]);
            entry('s', i, q, m.latch_names[i]);
            if (report) {
                *report << "  state " << m.latch_names[i] << ": " << q;
                if (t == 0) *report << " initial=" << (l.init == l.q ? "unconstrained" : l.init == 1 ? "1" : "0");
                else *report << " = previous D " << previous[i]
                    << "; clauses: (" << -q << ' ' << previous[i] << ") ("
                    << q << ' ' << -previous[i] << ')';
                *report << '\n';
            }
        }
        // Include final-frame inputs: the selected property may depend on them.
        for (size_t i = 0; i < m.inputs.size(); ++i) {
            int lit = frame[m.inputs[i] / 2] = cb.new_var();
            entry('i', i, lit, m.input_names[i]);
            if (report) *report << "  input " << m.input_names[i] << ": " << lit << '\n';
        }
        for (auto g : m.gates) {
            int a = resolve(g.a), b = resolve(g.b);
            int y = frame[g.lhs / 2] = cb.and2(a, b);
            if (report) *report << "  gate " << y << " = AND(" << a << ", " << b << ")\n";
        }
        previous.clear();
        for (auto l : m.latches) previous.push_back(resolve(l.d));
        if (report) for (size_t i = 0; i < previous.size(); ++i)
            *report << "  D " << m.latch_names[i] << ": " << previous[i] << '\n';
        int bad = resolve(property);
        entry('b', 0, bad, "bad");
        if (t == depth) cb.add_clause({bad});
        if (report) *report << "  bad: " << bad << (t == depth ? " (asserted)\n\n" : " (not asserted)\n\n");
    }
}
