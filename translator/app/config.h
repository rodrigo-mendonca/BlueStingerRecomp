#pragma once
// Leitura de config/<jogo>.toml para o tradutor.
#include "analysis/analysis.h"

#include <filesystem>
#include <set>
#include <string>

namespace bs {

struct GameConfig {
    std::string        executable = "1ST_READ.BIN";
    uint32_t           base       = 0;
    analysis::Config   analysis;
    std::vector<analysis::Relocated> relocated;  // [[relocated]]: copiado para a imagem
    bool               inline_pool_constants = true;
    uint32_t           funcs_per_file        = 400;
    std::set<uint32_t> setjmp_funcs, longjmp_funcs;  // [threads]
    std::set<uint32_t> native_slots;                 // [native] slots (fronteiras migraveis)
};

GameConfig loadConfig(const std::filesystem::path& path);

} // namespace bs
