// Store queues (manual SH7750, 4.6 "Store Queues"), com a MMU desligada:
//   0xE0000000-0xE3FFFFFF  escrita vai para SQ0 (bit 5 = 0) ou SQ1 (bit 5 = 1),
//                          palavra (endereco & 0x1C) / 4
//   QACR0 0xFF000038, QACR1 0xFF00003C: bits 4-2 = bits 28-26 do endereco externo
//   pref @Rn (Rn na faixa): envia os 32 bytes da fila para
//                          (QACRn[4:2] << 26) | (Rn & 0x03FFFFE0)
// O boot do jogo escreve vertices/matrizes (floats) e os envia assim.
#include "memory/sh4_sq.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "pvr/ta.h"

namespace bs {
namespace {

constexpr uint32_t kQacr0 = 0xFF000038, kQacr1 = 0xFF00003C;

uint32_t g_sq[2][8] = {};
uint32_t g_qacr[2]  = {};

bool inSq(uint32_t a) { return (a & 0xFC000000) == 0xE0000000; }

} // namespace

void sh4_sq_reset() {
    for (auto& q : g_sq)
        for (auto& w : q) w = 0;
    g_qacr[0] = g_qacr[1] = 0;
}

uint32_t sh4_sq_target(uint32_t addr) {
    const uint32_t q = (addr >> 5) & 1;
    return ((g_qacr[q] & 0x1C) << 24) | (addr & 0x03FFFFE0);
}

bool sh4_sq_read(uint32_t a, unsigned size, uint32_t& v) {
    if ((a == kQacr0 || a == kQacr1) && size == 4) {
        v = g_qacr[a == kQacr1];
        return true;
    }
    if (inSq(a) && size == 4) {
        v = g_sq[(a >> 5) & 1][(a & 0x1C) >> 2];
        return true;
    }
    return false;
}

bool sh4_sq_write(uint32_t a, unsigned size, uint32_t v) {
    if ((a == kQacr0 || a == kQacr1) && size == 4) {
        g_qacr[a == kQacr1] = v & 0x1C;
        return true;
    }
    if (inSq(a) && size == 4) {
        g_sq[(a >> 5) & 1][(a & 0x1C) >> 2] = v;
        return true;
    }
    return false;
}

void sh4_sq_flush(Sh4Context* c, uint32_t addr) {
    const uint32_t* q      = g_sq[(addr >> 5) & 1];
    const uint32_t  target = sh4_sq_target(addr);
    if ((target & 0x1C000000) == 0x0C000000) {  // area 3: RAM
        for (int i = 0; i < 8; ++i) sh4_write32(c, target + 4 * i, q[i]);
        return;
    }
    if (ta_fifo_write(c, target, q)) return;  // area 4: TA e caminho direto de textura
    sh4_fatal(c, "store queue para destino ainda nao implementado", c->pc, target);
}

} // namespace bs
