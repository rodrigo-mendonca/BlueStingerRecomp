// Texturas do PowerVR2 (documentacao de hardware via KallistiOS; Flycast so como
// referencia de comportamento). TCW:
//   bit 31 mipmap, 30 VQ, 29-27 formato (0 1555, 1 565, 2 4444, 3 YUV422, 4 bump,
//   5 paleta 4 bpp, 6 paleta 8 bpp), 26 ordem (0 twiddled, 1 linear; nas paletas,
//   26-21 = seletor de paleta), 25 stride select, 20-0 endereco (unidades de 8 bytes,
//   no caminho de 64 bits).
// Paleta: 1024 entradas em 0x005F9000; formato por PAL_RAM_CTRL (0 1555, 1 565,
// 2 4444, 3 8888). 4 bpp usa o bloco (seletor & 0x3F) * 16; 8 bpp, (seletor >> 4) * 256.
// M4: sem mipmap, VQ, YUV e bump (registrados e omitidos pelo rasterizador).
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
    const uint32_t fmt = (t.tcw >> 27) & 7;
    if (t.tcw & 0xC0000000) return false;  // mipmap, VQ
    return fmt <= 2 || fmt == 5 || fmt == 6;
}

uint32_t pvr_texel(const TexInfo& t, uint32_t x, uint32_t y) {
    const uint32_t fmt  = (t.tcw >> 27) & 7;
    const uint32_t base = (t.tcw & 0x1FFFFF) << 3;
    if (fmt == 5 || fmt == 6) {  // paleta: sempre twiddled
        const uint32_t sel = (t.tcw >> 21) & 0x3F;
        const uint32_t i   = pvr_twiddle(x, y, t.width, t.height);
        if (fmt == 5) {
            const uint8_t b = vram8_64(base + i / 2);
            return pvr_pal_entry(sel * 16 + ((i & 1) ? (b >> 4) : (b & 0xF)));
        }
        return pvr_pal_entry((sel >> 4) * 256 + vram8_64(base + i));
    }
    const bool     linear = (t.tcw >> 26) & 1;
    const uint32_t w      = linear && ((t.tcw >> 25) & 1) && t.stride ? t.stride : t.width;
    const uint32_t i      = linear ? y * w + x : pvr_twiddle(x, y, t.width, t.height);
    return pvr_texel16_to_argb(vram16_64(base + 2 * i), fmt);
}

} // namespace bs
