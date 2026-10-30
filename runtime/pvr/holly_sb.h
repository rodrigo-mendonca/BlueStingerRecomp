#pragma once
// Registradores do system block do Holly (0x005F6800-0x005F7FFF): configuracao
// de DMA (ch2, Sort, PVR, G1, G2), Maple e revisao do barramento. M4: guardam o
// valor escrito; os que precisam de comportamento ganham modelo quando o boot pede.
#include "cpu/sh4_context.h"

#include <string>

namespace bs {

void holly_sb_reset();

// true se `a` e um registrador do system block (o acesso foi tratado).
bool holly_sb_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool holly_sb_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

// N8.7 5b, so a validacao: o system block nativo em sombra no modelo do legado ("[system-block] ...\n"); vazio no
// produto (system/system_block_bus.cpp)
std::string holly_sb_summary();

} // namespace bs
