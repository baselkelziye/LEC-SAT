#include "cnf.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


// ============================================================
// I/O map representation
// ============================================================

struct SignalKey {
    std::string port;
    int bit;

    bool operator<(const SignalKey& other) const {

        if (port != other.port)
            return port < other.port;

        return bit < other.bit;
    }
};


using SignalMap =
    std::map<SignalKey, Lit>;


struct IoMap {
    SignalMap inputs;
    SignalMap outputs;
};


// ============================================================
// Read .iomap
//
// Format:
//
// i a      0 17 +
// i a      1 18 +
// o result 0 54 -
//
// ============================================================

static IoMap read_iomap(
    const std::string& path)
{
    std::ifstream in(path);

    if (!in)
        throw std::runtime_error(
            "cannot open iomap: " + path
        );


    IoMap io;

    std::string line;


    while (std::getline(in, line)) {

        if (line.empty())
            continue;


        std::istringstream iss(line);

        char direction;


        if (!(iss >> direction))
            continue;


        // Comment line.
        if (direction == 'c')
            continue;


        std::string port;
        int bit;
        int var;
        char polarity;


        if (!(iss
              >> port
              >> bit
              >> var
              >> polarity))
        {
            throw std::runtime_error(
                "invalid iomap line: " + line
            );
        }


        Lit lit =
            polarity == '+'
                ? var
                : -var;


        SignalKey key{
            port,
            bit
        };


        if (direction == 'i') {

            io.inputs[key] = lit;

        } else if (direction == 'o') {

            io.outputs[key] = lit;

        } else {

            throw std::runtime_error(
                "invalid iomap direction"
            );
        }
    }


    return io;
}


// ============================================================
// SAT model representation
//
// model[var] =
//   -1 unknown
//    0 false
//    1 true
//
// ============================================================

using Model =
    std::vector<int8_t>;


static Model read_model(
    const std::string& path,
    int minimum_vars)
{
    std::ifstream in(path);

    if (!in)
        throw std::runtime_error(
            "cannot open model: " + path
        );


    Model model(
        minimum_vars + 1,
        -1
    );


    std::string token;


    while (in >> token) {

        if (token == "v")
            continue;


        int lit;


        try {

            lit = std::stoi(token);

        } catch (...) {

            continue;
        }


        if (lit == 0)
            continue;


        const int var =
            std::abs(lit);


        if (var >=
            static_cast<int>(model.size()))
        {
            model.resize(
                var + 1,
                -1
            );
        }


        model[var] =
            lit > 0 ? 1 : 0;
    }


    return model;
}


// ============================================================
// Miter variable translation
//
// LHS:
//      1 ... lhs_n
//
// RHS:
//      lhs_n + 1 ...
//
// Therefore RHS variable 5 becomes:
//
//      lhs_n + 5
//
// ============================================================

static Lit shift_lit(
    Lit lit,
    int offset)
{
    return lit > 0
        ? lit + offset
        : lit - offset;
}


// ============================================================
// Evaluate a logical literal from SAT model
// ============================================================

static bool value_of_lit(
    const Model& model,
    Lit lit)
{
    const int var =
        std::abs(lit);


    if (var <= 0 ||
        var >= static_cast<int>(model.size()) ||
        model[var] < 0)
    {
        throw std::runtime_error(
            "model does not assign variable "
            + std::to_string(var)
        );
    }


    const bool variable_value =
        model[var] != 0;


    return lit > 0
        ? variable_value
        : !variable_value;
}


// ============================================================
// Group individual bits into Verilog-style ports
//
// a[0]
// a[1]
// a[2]
// a[3]
//
// becomes:
//
// a = 4'bxxxx
//
// ============================================================

struct PortValue {
    std::map<int, bool> bits;
};


using PortMap =
    std::map<std::string, PortValue>;


static PortMap decode_signals(
    const SignalMap& signals,
    const Model& model,
    int offset)
{
    PortMap ports;


    for (const auto& [key, lit] : signals) {

        const Lit miter_lit =
            shift_lit(
                lit,
                offset
            );


        ports[key.port]
            .bits[key.bit] =
                value_of_lit(
                    model,
                    miter_lit
                );
    }


    return ports;
}


static std::string binary_string(
    const PortValue& value)
{
    if (value.bits.empty())
        return "";


    const int max_bit =
        value.bits.rbegin()->first;

    const int min_bit =
        value.bits.begin()->first;


    std::string result;


    for (int bit = max_bit;
         bit >= min_bit;
         --bit)
    {
        auto it =
            value.bits.find(bit);


        if (it == value.bits.end()) {

            result.push_back('x');

        } else {

            result.push_back(
                it->second
                    ? '1'
                    : '0'
            );
        }
    }


    return result;
}


static int width_of(
    const PortValue& value)
{
    if (value.bits.empty())
        return 0;


    return
        value.bits.rbegin()->first
        -
        value.bits.begin()->first
        + 1;
}


// ============================================================
// Find which output bits differ
// ============================================================

static std::vector<int> differing_bits(
    const PortValue& lhs,
    const PortValue& rhs)
{
    std::vector<int> diff;


    for (const auto& [bit, lhs_value]
         : lhs.bits)
    {
        auto it =
            rhs.bits.find(bit);


        if (it == rhs.bits.end() ||
            it->second != lhs_value)
        {
            diff.push_back(bit);
        }
    }


    return diff;
}


// ============================================================
// Main
//
// Usage:
//
// cex_decoder
//    model.txt
//    lhs.cnf
//    lhs.iomap
//    rhs.cnf
//    rhs.iomap
//
// ============================================================

int main(int argc, char** argv) {

    if (argc != 6) {

        std::cerr
            << "usage: "
            << argv[0]
            << " <model.txt>"
            << " <lhs.cnf>"
            << " <lhs.iomap>"
            << " <rhs.cnf>"
            << " <rhs.iomap>\n";

        return 1;
    }


    try {

        const std::string model_path =
            argv[1];

        const std::string lhs_cnf_path =
            argv[2];

        const std::string lhs_iomap_path =
            argv[3];

        const std::string rhs_cnf_path =
            argv[4];

        const std::string rhs_iomap_path =
            argv[5];


        // ----------------------------------------------------
        // Read original circuit information.
        // ----------------------------------------------------

        const CnfFormula lhs_cnf =
            read_dimacs(lhs_cnf_path);

        const CnfFormula rhs_cnf =
            read_dimacs(rhs_cnf_path);


        const IoMap lhs_io =
            read_iomap(lhs_iomap_path);

        const IoMap rhs_io =
            read_iomap(rhs_iomap_path);


        /*
         * IMPORTANT:
         *
         * build_miter() uses:
         *
         * RHS_offset = lhs.num_vars
         */
        const int rhs_offset =
            lhs_cnf.num_vars;


        const int minimum_model_vars =
            lhs_cnf.num_vars
            +
            rhs_cnf.num_vars;


        const Model model =
            read_model(
                model_path,
                minimum_model_vars
            );


        // ----------------------------------------------------
        // Decode inputs.
        // ----------------------------------------------------

        const PortMap lhs_inputs =
            decode_signals(
                lhs_io.inputs,
                model,
                0
            );


        const PortMap rhs_inputs =
            decode_signals(
                rhs_io.inputs,
                model,
                rhs_offset
            );


        // ----------------------------------------------------
        // Decode outputs.
        // ----------------------------------------------------

        const PortMap lhs_outputs =
            decode_signals(
                lhs_io.outputs,
                model,
                0
            );


        const PortMap rhs_outputs =
            decode_signals(
                rhs_io.outputs,
                model,
                rhs_offset
            );


        // ====================================================
        // Print counterexample
        // ====================================================

        std::cout
            << "\n"
            << "==============================\n"
            << "Counterexample\n"
            << "==============================\n";


        std::cout
            << "\nInputs:\n";


        for (const auto& [port, lhs_value]
             : lhs_inputs)
        {
            auto rhs_it =
                rhs_inputs.find(port);


            if (rhs_it == rhs_inputs.end())
                throw std::runtime_error(
                    "RHS input missing: " + port
                );


            const std::string lhs_bits =
                binary_string(lhs_value);

            const std::string rhs_bits =
                binary_string(rhs_it->second);


            /*
             * Because our miter constrains corresponding
             * inputs to be equal, these MUST match.
             */
            if (lhs_bits != rhs_bits) {

                throw std::runtime_error(
                    "miter model violates equal-input "
                    "constraint for port: "
                    + port
                );
            }


            std::cout
                << "  "
                << port
                << " = "
                << width_of(lhs_value)
                << "'b"
                << lhs_bits
                << '\n';
        }


        std::cout
            << "\nOutputs:\n";


        bool found_difference = false;


        for (const auto& [port, lhs_value]
             : lhs_outputs)
        {
            auto rhs_it =
                rhs_outputs.find(port);


            if (rhs_it == rhs_outputs.end())
                throw std::runtime_error(
                    "RHS output missing: " + port
                );


            const PortValue& rhs_value =
                rhs_it->second;


            const std::vector<int> diff =
                differing_bits(
                    lhs_value,
                    rhs_value
                );


            const bool different =
                !diff.empty();


            if (different)
                found_difference = true;


            std::cout
                << "  "
                << port
                << ":\n";


            std::cout
                << "    lhs = "
                << width_of(lhs_value)
                << "'b"
                << binary_string(lhs_value)
                << '\n';


            std::cout
                << "    rhs = "
                << width_of(rhs_value)
                << "'b"
                << binary_string(rhs_value);


            if (different) {

                std::cout
                    << "   <-- DIFFERENT at bit";

                if (diff.size() > 1)
                    std::cout << "s";

                std::cout << " ";


                for (std::size_t i = 0;
                     i < diff.size();
                     ++i)
                {
                    if (i != 0)
                        std::cout << ",";

                    std::cout
                        << diff[i];
                }
            }


            std::cout << '\n';
        }


        if (!found_difference) {

            throw std::runtime_error(
                "SAT model contains no output mismatch"
            );
        }


        return 0;

    } catch (const std::exception& e) {

        std::cerr
            << "cex error: "
            << e.what()
            << '\n';

        return 1;
    }
}