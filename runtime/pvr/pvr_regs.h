#pragma once
// Registradores do PowerVR (Holly), area 0: 0x005F8000-0x005F9FFF.
// M4: guardam o valor escrito; SPG_STATUS e derivado do contador de ciclos.
#include "cpu/sh4_context.h"

#include <string>

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

// Banco de registradores (0x005F8000-0x005F9FFF, 2048 palavras) como o guest le,
// para comparacao com o oraculo; SPG_STATUS depende de `cycles`.
void pvr_regs_dump(Sh4Context* c, uint32_t* out);

// Leitura de um registrador pelo hardware (rasterizador), sem o diagnostico de
// "leitura sem modelo". `a` = 0x005F8000..0x005F9FFF.
uint32_t pvr_reg(uint32_t a);
// Entrada da paleta (0..1023) em ARGB8888, conforme PAL_RAM_CTRL.
uint32_t pvr_pal_entry(uint32_t index);
// N8.7 3b: sobe a cada escrita na paleta ou no PAL_RAM_CTRL (o cache de texturas com paleta a usa)
uint64_t pvr_palette_generation();
// Carrega o banco inteiro (estado deixado pelo BIOS; ID, REVISION e SPG_STATUS sao
// calculados e ignorados).
void pvr_regs_load(const uint32_t* words);

// Diagnostico/testes: chamado a cada escrita em STARTRENDER, antes de o valor ser
// guardado (n = numero da escrita, a partir de 1). Pode lancar (checkpoint do bs-diff).
// Fim de render pendente (ciclo em que ISTNRM 0/1/2 acendem) ou 0 se nenhum; o
// controlador do Holly consome com pvr_render_done_take.
uint64_t pvr_render_done_due();
void     pvr_render_done_take();

using PvrStartRenderHook = void (*)(Sh4Context* c, int n);
void pvr_set_startrender_hook(PvrStartRenderHook hook);

// O inicio de `line` (em algum campo) caiu no intervalo (from, to]?
bool pvr_spg_crossed(uint64_t from, uint64_t to, uint32_t line);
// Primeiro instante depois de `after` em que a linha `line` e cruzada (a mesma conta de
// pvr_spg_crossed); kSpgNever se o campo nunca chega a ela. N2a: agenda o VBlank na fila.
constexpr uint64_t kSpgNever = ~0ull;
uint64_t           pvr_spg_next_crossing(uint64_t after, uint32_t line);
// N2b-2: primeiro instante depois de `after` em que a linha do feixe muda. SPG_STATUS (linha,
// vsync, campo) so muda nesses instantes: e o alvo de uma espera que le o SPG.
uint64_t pvr_spg_next_line_change(uint64_t after);
// Linhas das interrupcoes de VBlank (SPG_VBLANK_INT 0x005F80C4).
uint32_t pvr_vblank_in_line();
uint32_t pvr_vblank_out_line();

// N8.7 (validacao): as comparacoes do banco do legado com o FrameState nativo em sombra (pvr_regs.cpp)
std::string pvr_frame_state_summary();
// N8.7 3c (so validacao): o valor guardado no FrameState em sombra (a entrada do servico de exibicao em sombra)
uint32_t pvr_shadow_reg(uint32_t a);

} // namespace bs
