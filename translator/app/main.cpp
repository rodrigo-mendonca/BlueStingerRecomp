// bs-recomp: analise (descoberta de funcoes) e geracao de C++.
//   bs-recomp <config.toml> [--game-data DIR] [--out DIR] [--analysis DIR] [--analyze-only]
#include "app/recomp.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: bs-recomp <config.toml> [--game-data DIR] [--out DIR] [--analysis DIR] [--analyze-only]\n");
        return 2;
    }
    fs::path          gameData = "game-data";
    bs::RecompOptions opt;
    opt.config      = argv[1];
    opt.out         = "generated";
    opt.analysisDir = "analysis";
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--game-data" && i + 1 < argc) gameData = argv[++i];
        else if (a == "--out" && i + 1 < argc) opt.out = argv[++i];
        else if (a == "--analysis" && i + 1 < argc) opt.analysisDir = argv[++i];
        else if (a == "--analyze-only") opt.analyzeOnly = true;
        else { std::fprintf(stderr, "argumento desconhecido: %s\n", a.c_str()); return 2; }
    }
    try {
        opt.executable = gameData / bs::loadConfig(opt.config).executable;
        bs::recomp_run(opt, std::cout);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
