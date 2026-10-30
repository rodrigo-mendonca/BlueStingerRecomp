// N8.7 (docs/N8-7-KAMUI.md, incremento 4b): no produto, o DMAC do SH-4 (memory/sh4_dmac.h) e a copia do canal 2 nativa
// (native/render/kamui/channel_dma). A origem e lida da memoria guest pelo caminho de sempre e cada bloco vai para a
// entrada da area 4 (pvr/ta.h), na mesma ordem. O fim (ISTNRM bit 19) continua com o system block, no banco de status.
// O DMAC do legado (memory/sh4_dmac.cpp) fica na validacao como referencia, com os ganchos da sonda grafica
// (debug/gfx_probe, que so existe na validacao; revisao Codex 20261029-134812).
#include "memory/sh4_dmac.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "pvr/ta.h"
#include "render/kamui/channel_dma.h"
#include "system/graphics_ports.h"

namespace bs {
namespace {

using native::render::kamui::ChannelDma;

ChannelDma g_dma;

uint32_t read(void* ctx, uint32_t a) { return sh4_read32(static_cast<Sh4Context*>(ctx), a); }

bool sink(void* ctx, uint32_t dest, const uint32_t* blk) {
    return area4_write(static_cast<Sh4Context*>(ctx), dest, blk);
}

} // namespace

void sh4_dmac_reset() { g_dma.reset(); }

bool sh4_dmac_read(uint32_t a, unsigned size, uint32_t& v) {
    if (size != 4 || !ChannelDma::contains(a)) return false;
    v = g_dma.reg(a);
    return true;
}

bool sh4_dmac_write(uint32_t a, unsigned size, uint32_t v) {
    if (size != 4 || !ChannelDma::contains(a)) return false;
    g_dma.write(a, v);
    return true;
}

bool channel2_start(Sh4Context* c, uint32_t& c2dstat, uint32_t& c2dlen, uint32_t& c2dst) {
    ChannelDma::SbRegs sb{c2dstat, c2dlen, c2dst};
    const uint32_t     dest = c2dstat;
    switch (g_dma.ch2_start(sb, read, sink, c)) {
    case ChannelDma::Result::Disabled: return false;
    case ChannelDma::Result::BadDest: sh4_fatal(c, "DMA do canal 2 para destino fora da area 4", c->pc, dest);
    case ChannelDma::Result::Done: break;
    }
    c2dstat = sb.c2dstat, c2dlen = sb.c2dlen, c2dst = sb.c2dst;
    return true;
}

std::string sh4_dmac_ch2_summary() { return {}; }  // a comparacao com o DMAC do legado so na validacao

} // namespace bs
