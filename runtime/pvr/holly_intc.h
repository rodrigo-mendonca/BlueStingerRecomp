#pragma once
// Controlador de interrupcoes do Holly (system block, 0x005F6900-0x005F693F).
// M4: ISTNRM com os eventos de VBlank derivados do contador de ciclos; mascaras
// (IML2/4/6) so guardam o valor, ainda sem entregar interrupcao a CPU.
#include "cpu/sh4_context.h"

namespace bs {

constexpr uint32_t kIstVblankIn  = 1u << 3;
constexpr uint32_t kIstVblankOut = 1u << 4;

void holly_intc_reset(uint64_t cycles = 0);

// Acende os eventos ate `cycles` com a configuracao de SPG atual. Chamado antes de
// qualquer escrita que mude a temporizacao, para nao aplicar a nova a tempo passado.
void holly_intc_sync(uint64_t cycles);

// true se `a` e um registrador do controlador (o acesso foi tratado).
bool holly_intc_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool holly_intc_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

} // namespace bs
