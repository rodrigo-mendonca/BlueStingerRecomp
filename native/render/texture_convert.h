#pragma once
// N7 (preparacao): conversao das texturas do PowerVR2 para RGBA8 linear, o que o renderer Vulkan carrega.
// Portavel, sem o runtime legado (native/ nao depende dele): as mesmas regras do rasterizador de
// referencia em software (pvr_texture.cpp do runtime legado), que os testes de integracao comparam bit a bit.
//   16 bits: ARGB1555, RGB565, ARGB4444 (expansao repetindo os bits altos); ARGB8888 da paleta.
//   Enderecamento twiddled (Morton: bit de y no par, de x no impar; blocos quadrados lado a lado na
//   dimensao maior).
// Formatos que o jogo USA (varredura dos 14 181 PVRT dos .NT, docs/N7-PLANO.md secao 12): so 16 bits
// (565, 4444, 1555) em linear/retangular, twiddled, VQ, VQ pequeno com mipmap, paleta de 4 bits e mipmaps.
// Nenhum YUV422, bump, 8888 nem paleta de 8 bits nos arquivos: nao convertidos aqui.
#include <cstdint>
#include <span>
#include <vector>

namespace bs::native::render {

enum class Texel16 : uint8_t { Argb1555 = 0, Rgb565 = 1, Argb4444 = 2 };

uint32_t texel16_to_argb(uint16_t t, Texel16 f);  // 0xAARRGGBB
uint32_t twiddle_index(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

// w x h texels de 16 bits (twiddled ou linear) -> ARGB8888 linear, de cima para baixo
bool convert16(std::span<const uint16_t> src, uint32_t w, uint32_t h, Texel16 f, bool twiddled, std::vector<uint32_t>& out);

// Um nivel de mipmap convertido (o 0 e o maior, como o Vulkan espera)
struct MipLevel {
    uint32_t              size = 0;  // lado (mipmaps so em texturas quadradas)
    std::vector<uint32_t> argb;
};

// Mipmaps de 16 bits twiddled: os niveis estao do menor (1x1) para o maior, com o 1x1 em +6 bytes
// (pvr_mip_offset). Devolve todos os niveis, do maior ao 1x1 (o legado so amostra o maior).
bool convert16_mips(std::span<const uint8_t> data, uint32_t w, Texel16 f, std::vector<MipLevel>& out);

// VQ: codebook de `entries` vetores de 4 texels de 16 bits (bloco 2x2, ordem (0,0), (0,1), (1,0), (1,1)) e
// depois 1 indice por bloco, twiddled em (w/2, h/2). VQ normal: 256 entradas (2 KB). VQ PEQUENO (arquivo):
// codebook mais curto, com o numero de entradas dado pelo proprio arquivo (vq_codebook_entries).
// Com mipmap, os indices dos niveis menores vem antes (1 byte no 1x1 e 2x2 juntos, pvr_mip_offset).
bool convert_vq(std::span<const uint8_t> data, uint32_t w, uint32_t h, Texel16 f, bool mipmap, uint32_t entries,
                std::vector<uint32_t>& out);
// Entradas do codebook de um VQ guardado em `bytes` bytes (cabecalho PVRT ja descontado): o que sobra
// depois dos indices, em vetores de 8 bytes. 0 se nao fechar (arquivo invalido).
uint32_t vq_codebook_entries(uint32_t bytes, uint32_t w, uint32_t h, bool mipmap);

// Paleta de 4 bits, twiddled: 2 indices por byte (o de indice par no nibble baixo); `palette` ja em ARGB8888
// (16 cores: o banco escolhido pelo seletor de paleta do desenho)
bool convert_pal4(std::span<const uint8_t> data, uint32_t w, uint32_t h, std::span<const uint32_t> palette,
                  std::vector<uint32_t>& out);

} // namespace bs::native::render
