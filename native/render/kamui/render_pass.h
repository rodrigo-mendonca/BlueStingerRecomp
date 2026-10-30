#pragma once
// N8.7 (docs/N8-7-KAMUI.md, incremento 4d): o que um render pedido por STARTRENDER le e grava fora das listas, a partir
// do estado do quadro (os registradores graficos, 3a) e da memoria das texturas (3b, layout de 32 bits, 8 MB):
//   - o tamanho do render (TA_GLOB_TILE_CLIP);
//   - o fundo: ISP/TSP/TCW e 3 vertices na memoria de parametros, em (PARAM_BASE & 0xF00000) + tag_address * 4; cada
//     vertice tem x, y, z, [u, v], cor base e [offset], com (3 + skip) palavras. A cor e a do primeiro vertice;
//   - os parametros do quadro da IR (fundo, fog, sombra, HALF_OFFSET, clamp...);
//   - o Region Array tipo 2 (FPU_PARAM_CFG bit 21), em que cada tile escolhe o modo de ordenacao: a IR usa um modo para
//     a tela inteira, e um tile com outro modo e avisado;
//   - a gravacao da imagem no framebuffer (FB_W_SOF1, no formato de FB_W_CTRL, com FB_W_LINESTRIDE e o clip).
// Sem estado: quem chama passa como ler um registrador e a memoria.
#include "render/render.h"

#include <cstdint>
#include <vector>

namespace bs::native::render::kamui {

struct RenderPass {
    static constexpr uint32_t kMemSize = 8u << 20;

    // leitura de um registrador grafico pelo endereco fisico (0x005F8000 + offset)
    using Reg = uint32_t (*)(void* ctx, uint32_t p);
    // as faixas do framebuffer gravadas (offset no layout de 32 bits, bytes)
    using Touch = void (*)(void* ctx, uint32_t off, uint32_t n);

    struct Size {
        uint32_t width = 0, height = 0;
    };
    struct Background {
        uint32_t color    = 0;      // RGB do primeiro vertice (o alfa e sempre 0xFF)
        bool     textured = false;  // fundo com textura (a IR ainda nao a desenha: o chamador avisa)
    };

    static Size       size(Reg reg, void* ctx);
    static Background background(Reg reg, void* ctx, const uint8_t* mem);
    static FrameParams frame_params(Reg reg, void* ctx, uint32_t width, uint32_t height, uint32_t background_rgb);
    // true se algum tile do Region Array tipo 2 pede o modo de ordenacao oposto ao global (`autosort`)
    static bool region_sort_mismatch(Reg reg, void* ctx, const uint8_t* mem, bool autosort);
    // grava `color` (w x h ARGB8888) no framebuffer; cada linha gravada e marcada por `touch` (com a volta no fim dos
    // 8 MB, se houver)
    static void write_back(Reg reg, void* ctx, const std::vector<uint32_t>& color, uint32_t w, uint32_t h, uint8_t* mem,
                           Touch touch);
};

} // namespace bs::native::render::kamui
