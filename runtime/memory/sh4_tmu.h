#pragma once
// Timer unit (TMU) do SH-4: tres contadores decrescentes de 32 bits com recarga.
// O valor e derivado do instante do provedor de tempo (cpu/time_hooks.h), sem eventos proprios.
#include "cpu/sh4_context.h"

namespace bs {

// Diagnostico N2b: hash do estado mutavel do TMU (TCR/UNF, TCOR, bases, TSTR).
uint64_t sh4_tmu_state_hash();

void sh4_tmu_reset();

// true se `a` pertence ao TMU (o acesso foi tratado).
bool sh4_tmu_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool sh4_tmu_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

} // namespace bs
