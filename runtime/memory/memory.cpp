// Caminho lento de memoria (tudo fora da RAM principal). M3: so registra o
// acesso; M4 implementa o mapa de regioes (BIOS/flash HLE, registradores,
// VRAM, AICA, store queues).
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "memory/sh4_ccn.h"
#include "memory/sh4_port.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"
#include "pvr/pvr_regs.h"

#include <cstdio>

namespace bs {
namespace {

void unmapped(const char* kind, uint32_t a, uint32_t v) {
    static int reported = 0;
    if (reported < 64) {
        std::fprintf(stderr, "[mem] %s nao mapeado: %08X (valor %08X)\n", kind, a, v);
        if (++reported == 64) std::fprintf(stderr, "[mem] ... demais acessos omitidos\n");
    }
}

// Registradores ja modelados, implementados conforme o boot os pede (M4).
bool hwRead(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    return sh4_port_read(a, size, v) || sh4_ccn_read(a, size, v) || pvr_reg_read(c, a, size, v) ||
           holly_intc_read(c, a, size, v) || holly_sb_read(c, a, size, v);
}
bool hwWrite(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    return sh4_port_write(a, size, v) || sh4_ccn_write(a, size, v) || pvr_reg_write(c, a, size, v) ||
           holly_intc_write(c, a, size, v) || holly_sb_write(c, a, size, v);
}

} // namespace

uint8_t sh4_read8_slow(Sh4Context*, uint32_t a) { unmapped("read8", a, 0); return 0; }
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
void sh4_write8_slow(Sh4Context*, uint32_t a, uint8_t v) { unmapped("write8", a, v); }
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
