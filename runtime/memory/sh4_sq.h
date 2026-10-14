#pragma once
// Store queues do SH-4 (SQ0/SQ1, 32 bytes cada) e o envio por `pref`.
#include "cpu/sh4_context.h"

namespace bs {

void sh4_sq_reset();

// Escritas em 0xE0000000-0xE3FFFFFF e QACR0/QACR1; true se o acesso foi tratado.
bool sh4_sq_read(uint32_t a, unsigned size, uint32_t& v);
bool sh4_sq_write(uint32_t a, unsigned size, uint32_t v);

// Destino externo do envio de `addr` (MMU desligada): area de QACRn + bits 25:5.
uint32_t sh4_sq_target(uint32_t addr);


} // namespace bs
