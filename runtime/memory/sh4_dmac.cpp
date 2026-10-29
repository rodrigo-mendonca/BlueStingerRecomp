// DMAC (manual SH7750, cap. 14): por canal n, SARn 0xFFA00000+0x10n, DARn +4,
// DMATCRn +8, CHCRn +0xC; DMAOR 0xFFA00040. CHCR: bit 0 DE (habilita), bit 1 TE (fim).
// DMAOR bit 0 DME (habilita o controlador). O canal 2 em modo DDT e alimentado pelo
// Holly: o boot do jogo grava SAR2 (origem na RAM) e CHCR2, e dispara por SB_C2DST.
#include "memory/sh4_dmac.h"

#include "cpu/sh4_mem.h"
#include "debug/gfx_probe.h"
#include "cpu/sh4_runtime.h"
#include "pvr/ta.h"

namespace bs {
namespace {

constexpr uint32_t kBase = 0xFFA00000, kDmaor = 0xFFA00040;
constexpr uint32_t kDe = 1u << 0, kTe = 1u << 1, kDme = 1u << 0;

uint32_t g_ch[4][4] = {};  // SAR, DAR, DMATCR, CHCR
uint32_t g_dmaor    = 0;

bool chanReg(uint32_t a, uint32_t*& r) {
    if (a < kBase || a >= kBase + 0x40 || (a & 3)) return false;
    r = &g_ch[(a - kBase) >> 4][((a - kBase) & 0xF) >> 2];
    return true;
}

} // namespace

void sh4_dmac_reset() {
    for (auto& ch : g_ch)
        for (auto& r : ch) r = 0;
    g_dmaor = 0;
}

bool sh4_dmac_read(uint32_t a, unsigned size, uint32_t& v) {
    if (size != 4) return false;
    uint32_t* r = nullptr;
    if (a == kDmaor) v = g_dmaor;
    else if (chanReg(a, r)) v = *r;
    else return false;
    return true;
}

bool sh4_dmac_write(uint32_t a, unsigned size, uint32_t v) {
    if (size != 4) return false;
    uint32_t* r = nullptr;
    if (a == kDmaor) g_dmaor = v;
    else if (chanReg(a, r)) *r = v;
    else return false;
    return true;
}

bool sh4_dmac_ch2_transfer(Sh4Context* c, uint32_t dest, uint32_t len) {
    uint32_t* ch = g_ch[2];
    if (!(g_dmaor & kDme) || !(ch[3] & kDe)) return false;
    const bool fifo = (dest & 0x01800000) == 0;  // FIFO de poligonos: o endereco nao avanca
    uint32_t   src  = ch[0];
    if (g_gfx_probe) gfx_probe_dma(c, src, dest, len);
    for (uint32_t off = 0; off < len; off += 32, src += 32) {
        uint32_t blk[8];
        for (int i = 0; i < 8; ++i) blk[i] = sh4_read32(c, src + 4 * i);
        if (g_gfx_probe) gfx_probe_ta(c, fifo ? dest : dest + off, GfxPath::Dma);
        if (!ta_fifo_write(c, fifo ? dest : dest + off, blk))
            sh4_fatal(c, "DMA do canal 2 para destino fora da area 4", c->pc, dest);
    }
    ch[0] = src;
    ch[2] = 0;
    ch[3] |= kTe;
    return true;
}

} // namespace bs
