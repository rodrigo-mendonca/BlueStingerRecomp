// Caminho lento de memoria (tudo fora da RAM principal). M3: so registra o
// acesso; M4 implementa o mapa de regioes (BIOS/flash HLE, registradores,
// VRAM, AICA, store queues).
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"

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

} // namespace

uint8_t sh4_read8_slow(Sh4Context*, uint32_t a) { unmapped("read8", a, 0); return 0; }
uint16_t sh4_read16_slow(Sh4Context* c, uint32_t a) {
    if (a & 1) sh4_address_error(c, a, false);
    unmapped("read16", a, 0);
    return 0;
}
uint32_t sh4_read32_slow(Sh4Context* c, uint32_t a) {
    if (a & 3) sh4_address_error(c, a, false);
    unmapped("read32", a, 0);
    return 0;
}
void sh4_write8_slow(Sh4Context*, uint32_t a, uint8_t v) { unmapped("write8", a, v); }
void sh4_write16_slow(Sh4Context* c, uint32_t a, uint16_t v) {
    if (a & 1) sh4_address_error(c, a, true);
    unmapped("write16", a, v);
}
void sh4_write32_slow(Sh4Context* c, uint32_t a, uint32_t v) {
    if (a & 3) sh4_address_error(c, a, true);
    unmapped("write32", a, v);
}

} // namespace bs
