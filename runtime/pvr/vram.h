#pragma once
// VRAM do PowerVR (8 MB, area 1). Guardada no layout do caminho de 32 bits
// (banco 0 em 0-4 MB, banco 1 em 4-8 MB); o caminho de 64 bits intercala as
// palavras de 32 bits entre os dois bancos.
#include "cpu/sh4_context.h"

#include <string>

namespace bs {

constexpr uint32_t kVramSize = 8u << 20;

uint8_t* vram_data();  // layout do caminho de 32 bits
void     vram_reset();

// Offset no layout de 32 bits para um offset do caminho de 64 bits.
inline uint32_t vram_offset64(uint32_t o) {
    o &= kVramSize - 1;
    return ((o & ~7u) >> 1) | (o & 3u) | ((o & 4u) ? kVramSize / 2 : 0u);
}

// true se `a` cai na area 1 (o acesso foi tratado). Acesso que atravessa o limite
// de uma palavra de 32 bits no caminho de 64 bits e invalido no hardware: fatal.
bool vram_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool vram_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

// N8.7 3b (docs/N8-7-KAMUI.md): quem escreve direto em vram_data() (o caminho direto de textura do TA, a gravacao do
// render no framebuffer, o conversor YUV da validacao) marca o intervalo (offset no layout de 32 bits); a geracao de um
// intervalo do caminho de 64 bits sobe a cada escrita que o toca (o cache de texturas a usa para saber se mudou)
void        vram_touch(uint32_t off, uint32_t n);
uint64_t    vram_generation64(uint32_t off64, uint32_t n);
std::string vram_heap_summary();  // validacao: a memoria nativa em sombra contra este modelo
void        vram_heap_check();    // validacao: compara as paginas tocadas desde a ultima vez (a cada render)

} // namespace bs
