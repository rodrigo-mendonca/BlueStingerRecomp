#pragma once
// DMAC do SH-4 (canais 0-3, DMAOR). M4: o canal 2, que o Holly usa para enviar listas
// e texturas ao TA (disparado por SB_C2DST).
#include "cpu/sh4_context.h"

namespace bs {

void sh4_dmac_reset();

bool sh4_dmac_read(uint32_t a, unsigned size, uint32_t& v);
bool sh4_dmac_write(uint32_t a, unsigned size, uint32_t v);

// Transferencia do canal 2 (SB_C2DST = 1): `len` bytes de SAR2 para `dest` (area 4:
// FIFO do TA ou caminho de textura). Atualiza SAR2/DMATCR2/CHCR2.TE. Devolve false se
// o canal nao estiver habilitado (DMAOR.DME e CHCR2.DE).
bool sh4_dmac_ch2_transfer(Sh4Context* c, uint32_t dest, uint32_t len);

} // namespace bs
