#pragma once
// Leitura de config/<jogo>.toml para o tradutor.
#include "analysis/analysis.h"

#include <filesystem>
#include <string>

namespace bs {

struct GameConfig {
    std::string        executable = "1ST_READ.BIN";
    uint32_t           base       = 0;
    analysis::Config   analysis;
    bool               inline_pool_constants = true;
    uint32_t           funcs_per_file        = 400;
};

GameConfig loadConfig(const std::filesystem::path& path);

} // namespace bs
