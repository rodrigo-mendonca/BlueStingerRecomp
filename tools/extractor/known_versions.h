#pragma once
// Versoes suportadas, identificadas pelo hash do executavel de boot (ver
// ARCHITECTURE.md secao 3). Contem apenas fatos (hash, tamanho), nao dados do jogo.
#include <cstdint>

namespace bs {

struct KnownVersion {
    const char* id;
    const char* product;
    const char* version;
    uint32_t    boot_size;
    const char* boot_sha256;  // 1ST_READ.BIN desembaralhado
};

inline constexpr KnownVersion kKnownVersions[] = {
    {"bluestinger_us_v1000", "T13001N", "V1.000", 1975556,
     "013301f3bd5e8305058f661ec3a0cddcb8468b524a2c5f4d8f88e4d523d21a50"},
};

} // namespace bs
