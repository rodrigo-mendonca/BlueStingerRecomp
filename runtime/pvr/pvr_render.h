#pragma once
// Rasterizador de referencia do PowerVR2 (software): executa o render pedido por
// STARTRENDER a partir das listas recebidas pelo TA e grava o resultado no framebuffer
// da VRAM (FB_W_SOF1, formato de FB_W_CTRL). Fiel ao modelo do hardware (buffer de
// tile de 32 bits, profundidade em 1/w, fundo, listas opaca/punch-through/translucida),
// com aproximacoes documentadas em pvr_render.cpp.
#include "cpu/sh4_context.h"

namespace bs {

struct RenderStats {
    uint32_t triangles = 0, sprites = 0, skipped = 0;  // skipped: formatos ainda nao suportados
    uint32_t width = 0, height = 0;
};

RenderStats pvr_render(Sh4Context* c);

// Testes: true se, desde a ultima chamada, algum worker da resolucao paralela calculou com
// MXCSR diferente do padrao (o render deve isolar o ambiente de ponto flutuante).
bool pvr_render_take_fp_violation();

} // namespace bs
