// N8.7: a decodificacao de texels da VRAM (a referencia do rasterizador do caminho do TA e do modo conferir), so no
// build de validacao (papel validacao). O produto converte as texturas pelos conversores nativos (texture_convert).
// Formatos: ver pvr_texture.cpp.
#include "pvr/pvr_texture.h"

#include "pvr/vram.h"

#include <cstring>

namespace bs {

uint32_t pvr_pal_entry(uint32_t index);  // pvr_regs.cpp: paleta ja em ARGB8888

namespace {

uint8_t vram8_64(uint32_t off) { return vram_data()[vram_offset64(off)]; }

uint16_t vram16_64(uint32_t off) {
    uint16_t v;
    std::memcpy(&v, vram_data() + vram_offset64(off), 2);  // alinhado: nao cruza palavra
    return v;
}

uint32_t expand(uint32_t v, int bits) {  // replica os bits altos (5/6/4 -> 8)
    v <<= 8 - bits;
    return v | (v >> bits);
}

} // namespace

uint32_t pvr_twiddle(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const uint32_t m = w < h ? w : h;
    uint32_t       idx = 0;
    for (uint32_t bit = 0, s = 1; s < m; s <<= 1, ++bit) {
        idx |= ((y >> bit) & 1u) << (2 * bit);
        idx |= ((x >> bit) & 1u) << (2 * bit + 1);
    }
    // blocos quadrados lado a lado no sentido da dimensao maior
    const uint32_t block = (w > h ? x / m : y / m);
    return block * m * m + idx;
}

uint32_t pvr_texel16_to_argb(uint16_t t, uint32_t format) {
    uint32_t a, r, g, b;
    switch (format) {
    case 0:  // 1555
        a = (t & 0x8000) ? 0xFF : 0;
        r = expand((t >> 10) & 0x1F, 5);
        g = expand((t >> 5) & 0x1F, 5);
        b = expand(t & 0x1F, 5);
        break;
    case 1:  // 565
        a = 0xFF;
        r = expand((t >> 11) & 0x1F, 5);
        g = expand((t >> 5) & 0x3F, 6);
        b = expand(t & 0x1F, 5);
        break;
    default:  // 4444
        a = expand((t >> 12) & 0xF, 4);
        r = expand((t >> 8) & 0xF, 4);
        g = expand((t >> 4) & 0xF, 4);
        b = expand(t & 0xF, 4);
        break;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// YUV422: cada palavra de 32 bits guarda 2 pixels (U, Y0, V, Y1). Conversao do PowerVR2
// (documentacao de hardware): R = Y + 11/8 (V-128), G = Y - 11/32 (U-128) - 11/16 (V-128),
// B = Y + 55/32 (U-128).
uint32_t pvr_yuv_to_argb(uint32_t y, uint32_t u, uint32_t v) {
    const int yi = int(y), ui = int(u) - 128, vi = int(v) - 128;
    auto      clamp = [](int x) { return uint32_t(x < 0 ? 0 : x > 255 ? 255 : x); };
    const uint32_t r = clamp(yi + (11 * vi) / 8);
    const uint32_t g = clamp(yi - (11 * ui) / 32 - (11 * vi) / 16);
    const uint32_t b = clamp(yi + (55 * ui) / 32);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

uint32_t pvr_texel(const TexInfo& t, uint32_t x, uint32_t y) {
    const uint32_t fmt    = (t.tcw >> 27) & 7;
    const bool     mipmap = t.tcw >> 31, vq = (t.tcw >> 30) & 1;
    const uint32_t base   = (t.tcw & 0x1FFFFF) << 3;
    if (vq) {
        // VQ: codebook de 256 entradas x 4 texels de 16 bits (2x2, ordem twiddled: (0,0),
        // (0,1), (1,0), (1,1)), depois 1 indice por bloco 2x2, twiddled em (w/2, h/2)
        const uint32_t idxBase = base + 2048 + (mipmap ? pvr_mip_offset(fmt, true, t.width) : 0);
        const uint32_t code    = vram8_64(idxBase + pvr_twiddle(x / 2, y / 2, t.width / 2, t.height / 2));
        const uint32_t sub     = ((x & 1) << 1) | (y & 1);
        return pvr_texel16_to_argb(vram16_64(base + code * 8 + sub * 2), fmt);
    }
    if (fmt == 5 || fmt == 6) {  // paleta: sempre twiddled
        const uint32_t sel = (t.tcw >> 21) & 0x3F;
        const uint32_t i   = pvr_twiddle(x, y, t.width, t.height);
        if (fmt == 5) {
            const uint8_t b = vram8_64(base + i / 2);
            return pvr_pal_entry(sel * 16 + ((i & 1) ? (b >> 4) : (b & 0xF)));
        }
        return pvr_pal_entry((sel >> 4) * 256 + vram8_64(base + (mipmap ? pvr_mip_offset(6, false, t.width) : 0) + i));
    }
    if (mipmap) {  // 16 bits twiddled, maior nivel
        const uint32_t i = pvr_twiddle(x, y, t.width, t.height);
        return pvr_texel16_to_argb(vram16_64(base + pvr_mip_offset(fmt, false, t.width) + 2 * i), fmt);
    }
    const bool     linear = (t.tcw >> 26) & 1;
    const uint32_t w      = linear && ((t.tcw >> 25) & 1) && t.stride ? t.stride : t.width;
    if (fmt == 3) {  // YUV422 linear: par de pixels (x par, x+1) na mesma palavra
        const uint32_t pair = base + 2 * (y * w + (x & ~1u));
        uint32_t       word;
        std::memcpy(&word, vram_data() + vram_offset64(pair), 4);
        const uint32_t yy = (x & 1) ? (word >> 24) : ((word >> 8) & 0xFF);
        return pvr_yuv_to_argb(yy, word & 0xFF, (word >> 16) & 0xFF);
    }
    const uint32_t i      = linear ? y * w + x : pvr_twiddle(x, y, t.width, t.height);
    return pvr_texel16_to_argb(vram16_64(base + 2 * i), fmt);
}

} // namespace bs
