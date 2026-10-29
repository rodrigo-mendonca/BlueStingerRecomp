#pragma once
// DMAC do SH-4 (canais 0-3, DMAOR). M4: o canal 2, que o Holly usa para enviar listas
// e texturas ao TA (disparado por SB_C2DST).
// N8.7 4b (docs/N8-7-KAMUI.md): no produto, a copia do canal 2 nativa (system/dma_bus, sobre
// native/render/kamui/channel_dma); o DMAC do legado (sh4_dmac.cpp) so na validacao, como referencia.
#include "cpu/sh4_context.h"

#include <string>

namespace bs {

void sh4_dmac_reset();

bool sh4_dmac_read(uint32_t a, unsigned size, uint32_t& v);
bool sh4_dmac_write(uint32_t a, unsigned size, uint32_t v);

// SB_C2DST = 1: transferencia do canal 2 de C2DLEN bytes de SAR2 para C2DSTAT (area 4: FIFO do
// TA ou caminho de textura). No fim, SAR2 avanca, DMATCR2 zera e CHCR2.TE acende; C2DSTAT avanca e
// C2DLEN e C2DST zeram. Devolve false (nada muda) se o canal nao estiver habilitado (DMAOR.DME e
// CHCR2.DE). Destino fora da area 4 e erro explicito. O fim (ISTNRM bit 19) fica com quem chama.
bool sh4_dmac_ch2_start(Sh4Context* c, uint32_t& c2dstat, uint32_t& c2dlen, uint32_t& c2dst);

// N8.7 4b, so a validacao: comparacao da copia nativa com o DMAC do legado ("[canal-2] ...\n"); vazio no produto
std::string sh4_dmac_ch2_summary();

} // namespace bs
