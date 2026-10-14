// Area 1 (documentacao de mapa de memoria do KallistiOS, referencia de comportamento):
//   0x04000000-0x04FFFFFF  VRAM, caminho de 64 bits (palavras intercaladas entre bancos)
//   0x05000000-0x05FFFFFF  VRAM, caminho de 32 bits (linear)
//   0x06000000/0x07000000  espelhos de 0x04/0x05
// Os 8 MB se repetem dentro de cada janela de 16 MB. O boot do jogo zera a VRAM
// inteira pelo caminho de 32 bits (0xA5000000-0xA57FFFFF).
#include "pvr/vram.h"

#include "cpu/sh4_runtime.h"

#include <cstring>
#include <vector>

namespace bs {
namespace {

std::vector<uint8_t> g_vram(kVramSize);

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
void     vram_reset() { std::fill(g_vram.begin(), g_vram.end(), uint8_t(0)); }

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
    std::memcpy(&g_vram[size_t(o)], &v, size);
    return true;
}

} // namespace bs
