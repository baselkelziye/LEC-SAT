#pragma once
// Frontend: Yosys JSON netlist (post `synth`, pre `aigmap`) -> CnfBuilder.
//
// Unlike the AIGER route, this netlist has several 2-input primitive gate
// types ($_AND_, $_OR_, $_XOR_, $_NAND_, $_NOR_, $_NOT_, ...). Cells are
// NOT guaranteed to appear in topological order in the JSON, so nets are
// resolved via a memoized recursive walk instead of a single linear pass.
//
// Inversion stays free here too: $_NOT_ never allocates a variable or
// clause, it just returns the negated literal of its input, and
// $_NAND_/$_NOR_ reuse and2()/or2() with the output literal's sign flipped
// -- same trick, no separate clause templates needed.

#include <stdexcept>
#include <string>
#include <unordered_map>

#include "json.hpp"
#include "tseitin.hpp"

using json = nlohmann::json;

class JsonNetlist {
public:
    JsonNetlist(const json& module, CnfBuilder& cb) : mod_(module), cb_(cb) {
        // Map each cell's single output net -> the cell that drives it.
        for (auto& [cell_name, cell] : mod_["cells"].items()) {
            const std::string& type = cell["type"];
            const auto& conns = cell["connections"];
            if (!conns.contains("Y")) throw std::runtime_error("cell without Y output: " + cell_name);
            int y_net = conns["Y"][0].get<int>();
            driver_type_[y_net] = type;
            driver_conns_[y_net] = &conns;
        }
    }

    // Resolve a single JSON "bit" value (either an int net id, or the
    // literal string "0"/"1" for a tied-off constant) to a CNF literal.
    int resolve_bit(const json& bit) {
        if (bit.is_string()) {
            return bit.get<std::string>() == "1" ? cb_.true_lit() : cb_.false_lit();
        }
        int net = bit.get<int>();
        auto memo_it = net_lit_.find(net);
        if (memo_it != net_lit_.end()) return memo_it->second;

        auto drv_it = driver_type_.find(net);
        if (drv_it == driver_type_.end()) {
            // No driver cell -> this net is a primary input. Fresh variable.
            int lit = cb_.new_var();
            net_lit_[net] = lit;
            return lit;
        }

        const std::string& type = drv_it->second;
        const json& conns = *driver_conns_[net];
        int lit;
        if (type == "$_NOT_") {
            int a = resolve_bit(conns["A"][0]);
            lit = -a; // free: no variable, no clause
        } else {
            int a = resolve_bit(conns["A"][0]);
            int b = resolve_bit(conns["B"][0]);
            if (type == "$_AND_") lit = cb_.and2(a, b);
            else if (type == "$_NAND_") lit = -cb_.and2(a, b);
            else if (type == "$_OR_") lit = cb_.or2(a, b);
            else if (type == "$_NOR_") lit = -cb_.or2(a, b);
            else if (type == "$_XOR_") lit = cb_.xor2(a, b);
            else if (type == "$_XNOR_") lit = -cb_.xor2(a, b);
            else if (type == "$_MUX_") {
                // Y = S ? B : A  (Yosys convention). No new clause
                // template: mux(a,b,s) = or2(and2(a,-s), and2(b,s)).
                int s = resolve_bit(conns["S"][0]);
                lit = cb_.or2(cb_.and2(a, -s), cb_.and2(b, s));
            }
            else throw std::runtime_error("unsupported cell type: " + type);
        }
        net_lit_[net] = lit;
        return lit;
    }

private:
    const json& mod_;
    CnfBuilder& cb_;
    std::unordered_map<int, std::string> driver_type_;
    std::unordered_map<int, const json*> driver_conns_;
    std::unordered_map<int, int> net_lit_;
};

inline void run_json_frontend(const std::string& json_path, const std::string& top_module,
                               CnfBuilder& cb) {
    std::ifstream f(json_path);
    if (!f) throw std::runtime_error("cannot open json file: " + json_path);
    json root;
    f >> root;

    if (!root["modules"].contains(top_module))
        throw std::runtime_error("module not found in json: " + top_module);
    const json& mod = root["modules"][top_module];

    JsonNetlist net(mod, cb);

    // Resolve outputs first so every internal net gets pulled in via
    // recursion; then record I/O in a stable, deterministic order.
    for (auto& [port_name, port] : mod["ports"].items()) {
        const std::string dir = port["direction"];
        if (dir != "output") continue;
        const auto& bits = port["bits"];
        for (size_t i = 0; i < bits.size(); i++) net.resolve_bit(bits[i]);
    }

    for (auto& [port_name, port] : mod["ports"].items()) {
        const std::string dir = port["direction"];
        const auto& bits = port["bits"];
        char d = (dir == "input") ? 'i' : 'o';
        for (size_t i = 0; i < bits.size(); i++) {
            int lit = net.resolve_bit(bits[i]);
            cb.add_io(d, port_name, static_cast<int>(i), lit);
        }
    }
}