#pragma once
// Controlador de interrupcoes do Holly (system block, 0x005F6900-0x005F693F).
// ISTNRM com os eventos de VBlank derivados do contador de ciclos e os eventos do PVR
// (fim de lista do TA, fim de render); as mascaras IML2/4/6 definem o nivel IRL.
#include "cpu/sh4_context.h"

namespace bs {

constexpr uint32_t kIstVblankIn  = 1u << 3;
constexpr uint32_t kIstVblankOut = 1u << 4;

// Acende bits de ISTNRM (eventos do PVR: fim de lista do TA, fim de render).
void holly_intc_raise(uint32_t bits);

void holly_intc_reset(uint64_t cycles = 0);

// Acende os eventos ate `cycles` com a configuracao de SPG atual. Chamado antes de
// qualquer escrita que mude a temporizacao, para nao aplicar a nova a tempo passado.
void holly_intc_sync(uint64_t cycles);
// VBlank-In desde o reset (diagnostico: trace de ordem, registro de leituras do N1).
uint64_t holly_vblank_count();

// Nivel IRL pedido ao SH-4 (6, 4, 2 ou 0): maior nivel cujas mascaras IMLn
// (NRM/EXT/ERR) tem algum bit pendente em ISTNRM/ISTEXT/ISTERR.
uint32_t holly_irl_level(uint64_t cycles);

// true se `a` e um registrador do controlador (o acesso foi tratado).
bool holly_intc_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool holly_intc_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

} // namespace bs
