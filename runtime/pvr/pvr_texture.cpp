// Texturas do PowerVR2 (documentacao de hardware via KallistiOS; Flycast so como
// referencia de comportamento). TCW:
//   bit 31 mipmap, 30 VQ, 29-27 formato (0 1555, 1 565, 2 4444, 3 YUV422, 4 bump,
//   5 paleta 4 bpp, 6 paleta 8 bpp), 26 ordem (0 twiddled, 1 linear; nas paletas,
//   26-21 = seletor de paleta), 25 stride select, 20-0 endereco (unidades de 8 bytes,
//   no caminho de 64 bits).
// Paleta: 1024 entradas em 0x005F9000; formato por PAL_RAM_CTRL (0 1555, 1 565,
// 2 4444, 3 8888). 4 bpp usa o bloco (seletor & 0x3F) * 16; 8 bpp, (seletor >> 4) * 256.
// Suportados: 16 bits e paletas (twiddled/linear), VQ 16 bits, mipmap (so o maior nivel),
// YUV422 linear. Faltam: YUV422 twiddled, bump e paleta 4 bpp com mipmap (registrados e
// omitidos pelo rasterizador).
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

bool pvr_texture_supported(const TexInfo& t) {
    const uint32_t fmt    = (t.tcw >> 27) & 7;
    const bool     mipmap = t.tcw >> 31, vq = (t.tcw >> 30) & 1;
    const bool     linear = fmt <= 4 && ((t.tcw >> 26) & 1);
    if (vq) return fmt <= 2 && !linear;                  // VQ: 16 bits, twiddled
    if (mipmap) return (fmt <= 2 && !linear) || fmt == 6;  // nivel maior; paleta 4 bpp com mipmap ainda nao
    if (fmt == 3) return linear;                         // YUV422 linear (conversor YUV); twiddled ainda nao
    return fmt <= 2 || fmt == 5 || fmt == 6;
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

// Mipmaps (texturas quadradas, twiddled): os niveis vem do menor (1x1) para o maior. So o
// maior nivel e amostrado (sem filtro entre niveis). Deslocamento do maior nivel de lado W,
// com T = (W*W - 1) / 3 texels nos niveis menores (1 + 4 + ... + (W/2)^2):
//   16 bits: 6 + 2T bytes (o 1x1 fica em 6); paleta 8 bpp: 3 + T; VQ (indices, 1 byte por
//   bloco 2x2): 1 + ((W/2)^2 - 1) / 3, depois do codebook de 2 KB.
uint32_t pvr_mip_offset(uint32_t fmt, bool vq, uint32_t w) {
    if (w < 2) return vq ? 0 : (fmt == 6 ? 3 : 6);
    if (vq) return 1 + ((w / 2) * (w / 2) - 1) / 3;
    const uint32_t t = (w * w - 1) / 3;
    return fmt == 6 ? 3 + t : 6 + 2 * t;
}

void pvr_texture_read(uint32_t off, uint8_t* out, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) out[i] = vram8_64(off + i);
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
