// Area 1 (documentacao de mapa de memoria do KallistiOS, referencia de comportamento):
//   0x04000000-0x04FFFFFF  VRAM, caminho de 64 bits (palavras intercaladas entre bancos)
//   0x05000000-0x05FFFFFF  VRAM, caminho de 32 bits (linear)
//   0x06000000/0x07000000  espelhos de 0x04/0x05
// Os 8 MB se repetem dentro de cada janela de 16 MB. O boot do jogo zera a VRAM
// inteira pelo caminho de 32 bits (0xA5000000-0xA57FFFFF).
#include "pvr/vram.h"

#include "cpu/sh4_runtime.h"
#include "render/kamui/texture_heap.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace bs {
namespace {

std::vector<uint8_t> g_vram(kVramSize);

// N8.7 3b (docs/N8-7-KAMUI.md): este modelo e a referencia; a memoria das texturas nativa (a do produto) roda em sombra,
// recebe cada escrita e e comparada pagina a pagina a cada render (as paginas tocadas desde a ultima comparacao). A
// geracao que o cache de texturas usa vem dela. Negativo (BS_TEXTURE_HEAP_MUTACAO=marca): os blocos do caminho direto de
// textura (16 bytes em cada banco) e as escritas da CPU nao marcam a pagina (o cache tem de acusar a textura velha)
native::render::kamui::TextureHeap g_shadow;
std::set<uint32_t>                 g_dirty;  // paginas tocadas desde a ultima comparacao
uint64_t                           g_checks = 0, g_mismatch = 0;
std::string                        g_first;

bool mutation_unmarked() {
    static const bool m = [] {
        const char* v = std::getenv("BS_TEXTURE_HEAP_MUTACAO");
        return v && std::string(v) == "marca";
    }();
    return m;
}

void dirty(uint32_t off, uint32_t n) {  // com a volta no fim dos 8 MB (revisao Codex 20261029-013355)
    constexpr uint32_t kPage = native::render::kamui::TextureHeap::kPage;
    if (!n) return;
    off &= kVramSize - 1;
    n                    = std::min<uint32_t>(n, kVramSize);
    const uint32_t first = std::min<uint32_t>(n, kVramSize - off);
    for (uint32_t p = off / kPage; p <= (off + first - 1) / kPage; ++p) g_dirty.insert(p);
    if (n > first)
        for (uint32_t p = 0; p <= (n - first - 1) / kPage; ++p) g_dirty.insert(p);
}

// Offset no layout de 32 bits, ou -1 se `a` nao e area 1.
int64_t locate(uint32_t a) {
    if (a >= 0xE0000000) return -1;
    const uint32_t phys = a & 0x1FFFFFFF;
    if ((phys >> 26) != 1) return -1;  // area 1 = 0x04000000-0x07FFFFFF
    const uint32_t o = phys & (kVramSize - 1);
    return (phys & 0x01000000) ? o : vram_offset64(o);
}

} // namespace

uint8_t* vram_data() { return g_vram.data(); }
void     vram_reset() {
    std::fill(g_vram.begin(), g_vram.end(), uint8_t(0));
    g_shadow.reset();
    g_dirty.clear();
}

void vram_touch(uint32_t off, uint32_t n) {
    off &= kVramSize - 1;
    n                    = std::min<uint32_t>(n, kVramSize);
    const uint32_t first = std::min<uint32_t>(n, kVramSize - off);
    // espelha os bytes do modelo (com a volta no fim) e marca
    std::memcpy(g_shadow.data() + off, g_vram.data() + off, first);
    if (n > first) std::memcpy(g_shadow.data(), g_vram.data(), n - first);
    if (!(mutation_unmarked() && n == 16)) g_shadow.touch(off, n);  // negativo: copia sem marcar
    dirty(off, n);
}

uint64_t vram_generation64(uint32_t off64, uint32_t n) { return g_shadow.generation64(off64, n); }

void vram_heap_check() {
    constexpr uint32_t kPage = native::render::kamui::TextureHeap::kPage;
    for (const uint32_t p : g_dirty) {
        ++g_checks;
        if (std::memcmp(g_vram.data() + size_t(p) * kPage, g_shadow.data() + size_t(p) * kPage, kPage) != 0) {
            ++g_mismatch;
            if (g_first.empty()) g_first = "pagina " + std::to_string(p);
        }
    }
    g_dirty.clear();
}

std::string vram_heap_summary() {
    return "[texture-heap] paginas comparadas " + std::to_string(g_checks) + ", diferentes " + std::to_string(g_mismatch) +
           (g_first.empty() ? std::string() : "; primeira: " + g_first) + "\n";
}

bool vram_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const int64_t o = locate(a);
    if (o < 0) return false;
    if ((uint64_t(o) & 3) + size > 4) sh4_fatal(c, "acesso a VRAM atravessa palavra de 32 bits", c->pc, a);
    v = 0;
    std::memcpy(&v, &g_vram[size_t(o)], size);
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
    std::memcpy(&g_vram[size_t(o)], &v, size);
    if (mutation_unmarked()) std::memcpy(g_shadow.data() + o, &v, size);  // negativo: copia sem marcar
    else g_shadow.write(uint32_t(o), &v, size);
    dirty(uint32_t(o), size);
    return true;
}

} // namespace bs
