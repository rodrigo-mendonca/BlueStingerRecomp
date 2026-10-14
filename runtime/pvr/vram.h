#pragma once
// VRAM do PowerVR (8 MB, area 1). Guardada no layout do caminho de 32 bits
// (banco 0 em 0-4 MB, banco 1 em 4-8 MB); o caminho de 64 bits intercala as
// palavras de 32 bits entre os dois bancos.
#include "cpu/sh4_context.h"

namespace bs {

constexpr uint32_t kVramSize = 8u << 20;

uint8_t* vram_data();  // layout do caminho de 32 bits
void     vram_reset();

// Offset no layout de 32 bits para um offset do caminho de 64 bits.
inline uint32_t vram_offset64(uint32_t o) {
    o &= kVramSize - 1;
    return ((o & ~7u) >> 1) | (o & 3u) | ((o & 4u) ? kVramSize / 2 : 0u);
}

// true se `a` cai na area 1 (o acesso foi tratado). Acesso que atravessa o limite
// de uma palavra de 32 bits no caminho de 64 bits e invalido no hardware: fatal.
bool vram_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool vram_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

} // namespace bs
