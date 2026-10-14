#pragma once
// Analise + geracao de C++ (o nucleo do bs-recomp), como biblioteca: usada pela ferramenta
// de linha de comando e pelo Builder.
#include "app/config.h"

#include <cstddef>
#include <filesystem>
#include <iosfwd>

namespace bs {

struct RecompOptions {
    std::filesystem::path config;        // config/<jogo>.toml
    std::filesystem::path executable;    // 1ST_READ.BIN desembaralhado
    std::filesystem::path out;           // C++ gerado
    std::filesystem::path analysisDir;   // relatorios (report.txt, functions.tsv, ...)
    bool                  analyzeOnly = false;
};

struct RecompResult {
    size_t functions = 0, files = 0, lines = 0, warnings = 0;
};

// Lanca std::runtime_error em erro. Resumo das etapas em `log`.
RecompResult recomp_run(const RecompOptions& opt, std::ostream& log);

} // namespace bs
