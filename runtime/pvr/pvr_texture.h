#pragma once
// Leitura de texturas do PowerVR2 na VRAM (caminho de 64 bits): formatos 16 bits,
// paleta de 4/8 bits, twiddled ou linear. Usado pelo rasterizador (pvr_render.cpp).
#include <cstdint>

namespace bs {

// Palavra de controle de textura (TCW) e o tamanho vindo do TSP.
struct TexInfo {
    uint32_t tcw    = 0;
    uint32_t width  = 8;  // 8 << TSP[5:3]
    uint32_t height = 8;  // 8 << TSP[2:0]
    uint32_t stride = 0;  // linear com "stride select": TEXT_CONTROL[4:0] * 32
};

// Indice twiddled (intercalacao de bits: bit 0 = y0, bit 1 = x0, ...) dentro de uma
// textura w x h; texturas retangulares sao quadrados de min(w,h) lado a lado.
uint32_t pvr_twiddle(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

// Converte um texel de 16 bits (formato do TCW: 0 1555, 1 565, 2 4444) para ARGB8888.
uint32_t pvr_texel16_to_argb(uint16_t t, uint32_t format);

// Y, U, V (0-255) para ARGB8888 opaco, com os coeficientes do PowerVR2.
uint32_t pvr_yuv_to_argb(uint32_t y, uint32_t u, uint32_t v);

// true se o formato/modo e suportado (os outros sao registrados e o poligono e omitido).
bool pvr_texture_supported(const TexInfo& t);

// Deslocamento (bytes; VQ: dos indices, depois do codebook) do maior nivel de um mipmap de lado w.
uint32_t pvr_mip_offset(uint32_t format, bool vq, uint32_t w);

// Texel (x, y) ja dentro do tamanho, como ARGB8888.
uint32_t pvr_texel(const TexInfo& t, uint32_t x, uint32_t y);

} // namespace bs
