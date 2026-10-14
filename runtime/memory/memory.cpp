// Caminho lento de memoria (tudo fora da RAM principal). M3: so registra o
// acesso; M4 implementa o mapa de regioes (BIOS/flash HLE, registradores,
// VRAM, AICA, store queues).
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "aica/aica_regs.h"
#include "cpu/sh4_intc.h"
#include "memory/sh4_ccn.h"
#include "memory/sh4_dmac.h"
#include "memory/sh4_port.h"
#include "memory/sh4_sq.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"
#include "pvr/pvr_regs.h"
#include "pvr/vram.h"

#include <cstdio>
#include <set>
#include <string>
#include <utility>

namespace bs {
namespace {

// Cada registrador distinto e registrado uma vez (area 0 e P4 por endereco; o resto,
// memorias como VRAM e FIFOs, por pagina de 64 KB), para enxergar todo o hardware
// que o boot toca sem inundar o log.
void unmapped(const char* kind, uint32_t a, uint32_t v) {
    static std::set<std::pair<std::string, uint32_t>> seen;
    const uint32_t phys = a & 0x1FFFFFFF;
    const bool     reg  = a >= 0xE0000000 || phys < 0x00800000;
    if (seen.size() >= 4096 || !seen.emplace(kind, reg ? a : (a & ~0xFFFFu)).second) return;
    std::fprintf(stderr, "[mem] %s nao mapeado: %08X (valor %08X)%s\n", kind, a, v, reg ? "" : " [pagina]");
}

// Registradores ja modelados, implementados conforme o boot os pede (M4).
bool hwRead(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    return vram_read(c, a, size, v) ||           // area 1
           pvr_reg_read(c, a, size, v) ||        // 0x005F8000
           holly_intc_read(c, a, size, v) ||     // 0x005F6900 (antes do system block)
           holly_sb_read(c, a, size, v) ||       // 0x005F6800
           aica_reg_read(c, a, size, v) ||       // 0x00702C00, RTC
           sh4_intc_read(a, size, v) ||          // P4: ICR/IPR, INTEVT
           sh4_port_read(a, size, v) ||          // P4: PCTRA/PDTRA
           sh4_sq_read(a, size, v) ||            // P4: store queues, QACR
           sh4_dmac_read(a, size, v) ||          // P4: DMAC
           sh4_ccn_read(a, size, v);             // P4: CCR, arrays de cache
}
bool hwWrite(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    return vram_write(c, a, size, v) ||
           pvr_reg_write(c, a, size, v) ||
           holly_intc_write(c, a, size, v) ||
           holly_sb_write(c, a, size, v) ||
           aica_reg_write(c, a, size, v) ||
           sh4_intc_write(a, size, v) ||
           sh4_port_write(a, size, v) ||
           sh4_sq_write(a, size, v) ||
           sh4_dmac_write(a, size, v) ||
           sh4_ccn_write(a, size, v);
}

} // namespace

uint8_t sh4_read8_slow(Sh4Context* c, uint32_t a) {
    if (uint32_t v; hwRead(c, a, 1, v)) return uint8_t(v);
    unmapped("read8", a, 0);
    return 0;
}
uint16_t sh4_read16_slow(Sh4Context* c, uint32_t a) {
    if (a & 1) sh4_address_error(c, a, false);
    if (uint32_t v; hwRead(c, a, 2, v)) return uint16_t(v);
    unmapped("read16", a, 0);
    return 0;
}
uint32_t sh4_read32_slow(Sh4Context* c, uint32_t a) {
    if (a & 3) sh4_address_error(c, a, false);
    if (uint32_t v; hwRead(c, a, 4, v)) return v;
    unmapped("read32", a, 0);
    return 0;
}
void sh4_write8_slow(Sh4Context* c, uint32_t a, uint8_t v) {
    if (hwWrite(c, a, 1, v)) return;
    unmapped("write8", a, v);
}
void sh4_write16_slow(Sh4Context* c, uint32_t a, uint16_t v) {
    if (a & 1) sh4_address_error(c, a, true);
    if (hwWrite(c, a, 2, v)) return;
    unmapped("write16", a, v);
}
void sh4_write32_slow(Sh4Context* c, uint32_t a, uint32_t v) {
    if (a & 3) sh4_address_error(c, a, true);
    if (hwWrite(c, a, 4, v)) return;
    unmapped("write32", a, v);
}

} // namespace bs
