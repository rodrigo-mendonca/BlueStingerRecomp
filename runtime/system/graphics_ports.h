#pragma once
// N8.7 (docs/N8-7-KAMUI.md, passo 5c): as entradas do caminho grafico que o codigo dos dois builds chama (store queues,
// Maple, system block, texturas, testes). No produto quem as atende sao os servicos nativos (system/*_bus.cpp); na
// validacao elas repassam aos modelos do legado, que continuam com os nomes antigos (ta_fifo_write, pvr_render,
// vram_data, holly_intc_raise, sh4_dmac_ch2_start) e ficam so la. O portao (tests/n8/check_papeis.cmake) proibe os
// nomes antigos no produto.
#include "cpu/sh4_context.h"
#include "pvr/pvr_render.h"

#if BS_VALIDACAO
#include "memory/sh4_dmac.h"
#include "pvr/holly_intc.h"
#include "pvr/ta.h"
#include "pvr/vram.h"
#endif

namespace bs {

#if BS_VALIDACAO
inline bool        area4_write(Sh4Context* c, uint32_t target, const uint32_t* data) {
    return ta_fifo_write(c, target, data);
}
inline RenderStats render_pass_run(Sh4Context* c) { return pvr_render(c); }
inline uint8_t*    texture_memory() { return vram_data(); }
inline void        event_status_raise(uint32_t bits) { holly_intc_raise(bits); }
inline bool        channel2_start(Sh4Context* c, uint32_t& c2dstat, uint32_t& c2dlen, uint32_t& c2dst) {
    return sh4_dmac_ch2_start(c, c2dstat, c2dlen, c2dst);
}
#else
// a entrada da area 4 (system/area4_bus): FIFO de poligonos (0x10000000) e caminho direto de textura (0x11000000), 32
// bytes por vez
bool        area4_write(Sh4Context* c, uint32_t target, const uint32_t* data);
// o render pedido por STARTRENDER (system/render_bus)
RenderStats render_pass_run(Sh4Context* c);
// a memoria das texturas (system/texture_heap_bus), no layout do caminho de 32 bits
uint8_t*    texture_memory();
// acende bits de ISTNRM no banco de status (system/event_bus)
void        event_status_raise(uint32_t bits);
// a copia do canal 2 pedida por SB_C2DST (system/dma_bus): de SB_C2DLEN bytes para SB_C2DSTAT; false se o canal nao
// estiver habilitado. O fim (ISTNRM bit 19) fica com quem chama
bool        channel2_start(Sh4Context* c, uint32_t& c2dstat, uint32_t& c2dlen, uint32_t& c2dst);
#endif

} // namespace bs
