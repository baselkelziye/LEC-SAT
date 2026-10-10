#include <iostream>
#include "bmc_aag.hpp"

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "usage: bmc_gen <file.aag> <depth> <output|bad> <index> <out_prefix>\n";
        return 1;
    }
    try {
        auto number = [](const char* s) {
            auto v = bmc_numbers(s);
            if (v.size() != 1) throw std::runtime_error("expected nonnegative integer");
            return v[0];
        };
        int depth = number(argv[2]), index = number(argv[4]);
        BmcAag model = read_bmc_aag(argv[1]);
        std::string kind = argv[3], prefix = argv[5];
        if (kind != "output" && kind != "bad") throw std::runtime_error("property kind must be output or bad");
        const auto& properties = kind == "output" ? model.outputs : model.bad;
        if (static_cast<size_t>(index) >= properties.size()) throw std::runtime_error("property index out of range");
        std::ofstream map(prefix + ".frames");
        if (!map) throw std::runtime_error("cannot write frame map");
        CnfBuilder cb("bmc");
        std::ofstream report(prefix + ".unroll.txt");
        if (!report) throw std::runtime_error("cannot write unrolling report");
        encode_bmc(model, depth, properties[index], cb, map, &report);
        report.close();
        if (!report) throw std::runtime_error("failed writing unrolling report");
        map.close();
        if (!map) throw std::runtime_error("failed writing frame map");
        cb.write_cnf(prefix + ".cnf", "exact-depth BMC");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
