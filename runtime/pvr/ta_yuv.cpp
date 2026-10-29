// N8.7: o conversor YUV do TA (o filme legado, Sofdec), so no build de validacao (papel validacao). No produto os
// filmes sao nativos (N8.5) e ta.cpp tem os stubs que dao o erro explicito.
#include "pvr/ta.h"

#include "cpu/sh4_runtime.h"
#include "pvr/holly_intc.h"
#include "pvr/vram.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bs {

// --- conversor YUV (0x10800000): macroblocos YUV420/YUV422 -> textura YUV422 (UYVY) na
// VRAM. Comportamento pela documentacao do PowerVR2 (KallistiOS: TA_YUV_TEX_*); o Flycast so
// como referencia de comportamento.
//   TA_YUV_TEX_BASE (0x005F8148): endereco da textura (caminho de 64 bits).
//   TA_YUV_TEX_CTRL (0x005F814C): bits 5-0 largura em macroblocos - 1 (U), 13-8 altura - 1
//     (V), bit 16: 0 = uma textura de (U+1)*16 x (V+1)*16, 1 = uma textura 16x16 por
//     macrobloco, em sequencia; bit 24: 0 = entrada YUV420, 1 = YUV422.
//   TA_YUV_TEX_CNT (0x005F8150): macroblocos convertidos.
// Macrobloco 420 (384 bytes): U 8x8, V 8x8, Y 4 blocos 8x8 (sup. esq., sup. dir., inf. esq.,
// inf. dir.). 422 (512 bytes): duas metades 16x8 (U 8x8, V 8x8, Y esq. 8x8, Y dir. 8x8). Saida: 2 pixels por palavra de 32
// bits, bytes U, Y0, V, Y1. Fim do quadro (todos os macroblocos): ISTNRM bit 6.
namespace {

struct Yuv {
    uint32_t base = 0, ctrl = 0;
    uint32_t count = 0;  // macroblocos convertidos neste quadro
    uint32_t fill  = 0;  // bytes do macrobloco atual
    uint8_t  mb[512]{};
} g_yuv;

void yuvConvert() {
    const bool     yuv422 = (g_yuv.ctrl >> 24) & 1;
    const bool     tiles  = (g_yuv.ctrl >> 16) & 1;
    const uint32_t wMb = (g_yuv.ctrl & 0x3F) + 1, hMb = ((g_yuv.ctrl >> 8) & 0x3F) + 1;
    const uint32_t mx = g_yuv.count % wMb, my = g_yuv.count / wMb;
    for (uint32_t row = 0; row < 16; ++row) {
        // 420: U 8x8, V 8x8, Y 4 blocos 8x8 (sup. esq., sup. dir., inf. esq., inf. dir.).
        // 422: duas metades 16x8, cada uma U 8x8, V 8x8, Y 2 blocos 8x8 (esq., dir.) (revisao
        // Codex 2026-10-15; ordem do exemplo yuv422 do KallistiOS).
        const uint8_t* half = g_yuv.mb + (yuv422 ? (row / 8) * 256 : 0);
        const uint8_t* u    = half;
        const uint8_t* v    = half + 64;
        const uint8_t* y    = half + 128;
        for (uint32_t col = 0; col < 16; col += 2) {
            auto Y = [&](uint32_t x) {
                const uint32_t block = yuv422 ? x / 8 : (row / 8) * 2 + x / 8;
                return y[block * 64 + (row % 8) * 8 + x % 8];
            };
            const uint32_t ci = (yuv422 ? row % 8 : row / 2) * 8 + col / 2;
            const uint32_t word = uint32_t(u[ci]) | uint32_t(Y(col)) << 8 | uint32_t(v[ci]) << 16 |
                                  uint32_t(Y(col + 1)) << 24;
            const uint32_t off = tiles ? g_yuv.count * 512 + (row * 16 + col) * 2
                                       : ((my * 16 + row) * wMb * 16 + mx * 16 + col) * 2;
            std::memcpy(vram_data() + vram_offset64(g_yuv.base + off), &word, 4);
            vram_touch(vram_offset64(g_yuv.base + off), 4);
        }
    }
    if (++g_yuv.count >= wMb * hMb) {
        g_yuv.count = 0;
        holly_intc_raise(1u << 6);  // fim da transferencia YUV
    }
}

} // namespace

void ta_yuv_reset(uint32_t base, uint32_t ctrl) {
    static const bool debug = std::getenv("BS_YUV_DEBUG") != nullptr;  // diagnostico
    if (debug && (base || ctrl)) std::fprintf(stderr, "[yuv] base %08X ctrl %08X\n", base, ctrl);
    g_yuv      = {};
    g_yuv.base = base & 0x00FFFFF8;
    g_yuv.ctrl = ctrl;
}

uint32_t ta_yuv_count() { return g_yuv.count; }

void ta_yuv_write(Sh4Context*, const uint32_t* data) {
    const uint32_t size = ((g_yuv.ctrl >> 24) & 1) ? 512 : 384;
    std::memcpy(g_yuv.mb + g_yuv.fill, data, 32);
    g_yuv.fill += 32;
    if (g_yuv.fill >= size) {
        // diagnostico: BS_YUV_DUMP=<arquivo> grava os macroblocos de entrada (ate 2000)
        static const char* dumpPath = std::getenv("BS_YUV_DUMP");
        static FILE*       dump     = dumpPath ? std::fopen(dumpPath, "wb") : nullptr;
        static int         dumped   = 0;
        if (dump && dumped < 2000) {
            std::fwrite(g_yuv.mb, 1, size, dump);
            if (++dumped == 2000) std::fclose(dump);
        }
        yuvConvert();
        g_yuv.fill = 0;
    }
}

} // namespace bs
