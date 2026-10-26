#pragma once
// Timer unit (TMU) do SH-4: tres contadores decrescentes de 32 bits com recarga.
// O valor e derivado do instante do provedor de tempo (cpu/time_hooks.h), sem eventos proprios.
#include "cpu/sh4_context.h"

namespace bs {

// Diagnostico N2b: hash do estado mutavel do TMU (TCR/UNF, TCOR, bases, TSTR).
uint64_t sh4_tmu_state_hash();

void sh4_tmu_reset();
// N8.1: o servico do tempo logico (memory/tmu_logical) entrega o estado ao modelo quando o jogo sai do contrato: o
// canal 0 com a base `base0` no instante `baseAt0` e os registradores comuns; os outros canais no estado de reset.
void sh4_tmu_adopt(uint8_t tocr, uint8_t tstr, uint32_t tcor0, uint32_t tcr0, uint32_t base0, uint64_t baseAt0);

// true se `a` pertence ao TMU (o acesso foi tratado).
bool sh4_tmu_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool sh4_tmu_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

} // namespace bs
