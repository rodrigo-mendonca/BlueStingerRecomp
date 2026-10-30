// N8.7 (docs/N8-7-KAMUI.md, D-K2 e incremento 3b): no produto, a area 1 (0x04000000-0x07FFFFFF, as duas vistas da
// VRAM) e a memoria das texturas nativa (native/render/kamui/texture_heap): os mesmos bytes nos mesmos enderecos, com a
// geracao por pagina que o cache de texturas usa. O modelo do legado (pvr/vram.cpp) fica na validacao como referencia.
#include "pvr/vram.h"

#include "cpu/sh4_runtime.h"
#include "render/kamui/texture_heap.h"
#include "system/graphics_ports.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace bs {
namespace {

native::render::kamui::TextureHeap g_heap;

// offset no armazenamento (layout de 32 bits), ou -1 se `a` nao e area 1 (0x04000000-0x07FFFFFF, espelhos 0x06/0x07)
int64_t locate(uint32_t a) {
    if (a >= 0xE0000000) return -1;
    const uint32_t phys = a & 0x1FFFFFFF;
    if ((phys >> 26) != 1) return -1;
    const uint32_t o = phys & (kVramSize - 1);
    return (phys & 0x01000000) ? o : vram_offset64(o);
}

} // namespace

uint8_t* texture_memory() { return g_heap.data(); }
void     vram_reset() { g_heap.reset(); }
void     vram_touch(uint32_t off, uint32_t n) { g_heap.touch(off, n); }
uint64_t vram_generation64(uint32_t off64, uint32_t n) { return g_heap.generation64(off64, n); }
std::string vram_heap_summary() { return {}; }  // a comparacao com o modelo do legado so existe na validacao
void        vram_heap_check() {}

bool vram_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const int64_t o = locate(a);
    if (o < 0) return false;
    if ((uint64_t(o) & 3) + size > 4) sh4_fatal(c, "acesso a VRAM atravessa palavra de 32 bits", c->pc, a);
    v = 0;
    std::memcpy(&v, g_heap.data() + size_t(o), size);
    return true;
}

bool vram_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const int64_t o = locate(a);
    if (o < 0) return false;
    if ((uint64_t(o) & 3) + size > 4) sh4_fatal(c, "acesso a VRAM atravessa palavra de 32 bits", c->pc, a);
    // Diagnostico: BS_VRAM_WATCH=<offset no layout de 32 bits> registra as escritas na palavra
    static const char*    watch = std::getenv("BS_VRAM_WATCH");
    static const uint32_t wofs  = watch ? uint32_t(std::strtoul(watch, nullptr, 0)) & ~3u : 0;
    if (watch && (uint32_t(o) & ~3u) == wofs)
        std::fprintf(stderr, "[vram] escrita %08X <- %08X (%u bytes) pc=%08X pr=%08X ciclo %llu\n", a, v, size, c->pc,
                     c->pr, (unsigned long long)c->cycles);
    g_heap.write(uint32_t(o), &v, size);
    return true;
}

} // namespace bs
