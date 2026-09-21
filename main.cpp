// cnf_gen: convert a combinational circuit to DIMACS CNF + a companion
// .iomap file, via either of two frontends:
//
//   --from-aag  <file.aag> <name> <out_prefix> [map_file]
//   --from-json <file.json> <top_module> <name> <out_prefix>
//
// Output: <out_prefix>.cnf, <out_prefix>.iomap
//
// See tseitin.hpp for the shared clause-encoding backend, aag_frontend.hpp
// and json_frontend.hpp for the two parsers.

#include <fstream>
#include <iostream>
#include <string>

#include "aag_frontend.hpp"
#include "json_frontend.hpp"
#include "tseitin.hpp"

static void usage(const char* prog) {
    std::cerr << "usage:\n"
              << "  " << prog << " --from-aag  <file.aag> <name> <out_prefix> [map_file]\n"
              << "  " << prog << " --from-json <file.json> <top_module> <name> <out_prefix>\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(argv[0]); return 1; }
    std::string mode = argv[1];

    try {
        if (mode == "--from-aag") {
            if (argc < 5) { usage(argv[0]); return 1; }
            std::string aag_path = argv[2], name = argv[3], out_prefix = argv[4];
            std::string map_path = (argc >= 6) ? argv[5] : "";
            if (map_path.empty())
                std::cerr << "warning: no map file given; inputs/outputs will be named "
                             "pi0.., po0.. (positional) instead of real port names\n";
            CnfBuilder cb(name);
            run_aag_frontend(aag_path, map_path, cb);
            cb.write_cnf(out_prefix + ".cnf", "aag frontend");
            cb.write_iomap(out_prefix + ".iomap");
            std::cerr << "wrote " << out_prefix << ".cnf (" << cb.num_vars() << " vars, "
                      << cb.num_clauses() << " clauses) and " << out_prefix << ".iomap\n";
        } else if (mode == "--from-json") {
            if (argc < 6) { usage(argv[0]); return 1; }
            std::string json_path = argv[2], top = argv[3], name = argv[4], out_prefix = argv[5];
            CnfBuilder cb(name);
            run_json_frontend(json_path, top, cb);
            cb.write_cnf(out_prefix + ".cnf", "json frontend");
            cb.write_iomap(out_prefix + ".iomap");
            std::cerr << "wrote " << out_prefix << ".cnf (" << cb.num_vars() << " vars, "
                      << cb.num_clauses() << " clauses) and " << out_prefix << ".iomap\n";
        } else {
            usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
