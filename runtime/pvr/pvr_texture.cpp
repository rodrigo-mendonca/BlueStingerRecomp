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
namespace {

uint8_t vram8_64(uint32_t off) { return vram_data()[vram_offset64(off)]; }

} // namespace

// N8.7: o produto so le os bytes e o formato (TCW); a decodificacao de texels da VRAM (pvr_texel e os conversores) e
// a referencia da validacao (pvr_texel.cpp): o produto converte pelos conversores nativos (native/render/texture_convert)
bool pvr_texture_supported(const TexInfo& t) {
    const uint32_t fmt    = (t.tcw >> 27) & 7;
    const bool     mipmap = t.tcw >> 31, vq = (t.tcw >> 30) & 1;
    const bool     linear = fmt <= 4 && ((t.tcw >> 26) & 1);
    if (vq) return fmt <= 2 && !linear;                  // VQ: 16 bits, twiddled
    if (mipmap) return (fmt <= 2 && !linear) || fmt == 6;  // nivel maior; paleta 4 bpp com mipmap ainda nao
    if (fmt == 3) return linear;                         // YUV422 linear (conversor YUV); twiddled ainda nao
    return fmt <= 2 || fmt == 5 || fmt == 6;
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

} // namespace bs
