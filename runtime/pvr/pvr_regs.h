#pragma once
// Registradores do PowerVR (Holly), area 0: 0x005F8000-0x005F9FFF.
// M4: guardam o valor escrito; SPG_STATUS e derivado do contador de ciclos.
#include "cpu/sh4_context.h"

namespace bs {

void pvr_regs_reset();

// true se `a` e um registrador do PVR (o acesso foi tratado).
bool pvr_reg_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool pvr_reg_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

// Posicao do feixe derivada de `cycles` (exposta para testes).
struct SpgPos {
    uint32_t line;   // 0 = inicio do vsync
    bool     vsync;
    bool     blank;
};
SpgPos pvr_spg_position(uint64_t cycles);

// O inicio de `line` (em algum campo) caiu no intervalo (from, to]?
bool pvr_spg_crossed(uint64_t from, uint64_t to, uint32_t line);
// Linhas das interrupcoes de VBlank (SPG_VBLANK_INT 0x005F80C4).
uint32_t pvr_vblank_in_line();
uint32_t pvr_vblank_out_line();

} // namespace bs
