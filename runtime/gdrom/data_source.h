#pragma once
// Fonte de setores do jogo nativo: os dados preparados pelo Builder (runtime_data.h), sem
// o CDI. O GD-ROM HLE continua o mesmo; so a origem dos setores muda.
#include "gdrom/gdrom.h"

#include <filesystem>
#include <memory>

namespace bs {

// Lanca disc::RuntimeDataError (dados ausentes, incompletos ou de outro formato).
// modAssets: pasta de mods de arquivos (mods/assets), opcional.
std::unique_ptr<DiscSource> open_runtime_data(const std::filesystem::path& dir,
                                              const std::filesystem::path& modAssets = {});

} // namespace bs
