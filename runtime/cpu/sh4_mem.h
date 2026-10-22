#pragma once
// Acesso a memoria do guest. RAM principal (area 3, com espelhos em P0-P3) tem
// caminho rapido inline; o resto vai para o despacho por regiao (memory/).
#include "cpu/sh4_context.h"
#include "cpu/write_watch.h"

#include <cstring>

namespace bs {

uint8_t  sh4_read8_slow(Sh4Context* c, uint32_t a);
uint16_t sh4_read16_slow(Sh4Context* c, uint32_t a);
uint32_t sh4_read32_slow(Sh4Context* c, uint32_t a);
void     sh4_write8_slow(Sh4Context* c, uint32_t a, uint8_t v);
void     sh4_write16_slow(Sh4Context* c, uint32_t a, uint16_t v);
void     sh4_write32_slow(Sh4Context* c, uint32_t a, uint32_t v);

constexpr uint32_t kRamMask = 0x00FFFFFF;

// Area 3 (0x0C000000-0x0FFFFFFF) vista de P0/P1/P2/P3; P4 (>= 0xE0000000) fica de fora.
inline bool sh4_is_ram(uint32_t a) { return (a & 0x1C000000) == 0x0C000000 && a < 0xE0000000; }

inline uint8_t sh4_read8(Sh4Context* c, uint32_t a) {
    if (sh4_is_ram(a)) return c->ram[a & kRamMask];
    return sh4_read8_slow(c, a);
}
// Word/longword desalinhado sai do caminho rapido; o caminho lento acusa address error.
inline uint16_t sh4_read16(Sh4Context* c, uint32_t a) {
    if (sh4_is_ram(a) && !(a & 1)) {
        uint16_t v;
        std::memcpy(&v, c->ram + (a & kRamMask), 2);
        return v;
    }
    return sh4_read16_slow(c, a);
}
inline uint32_t sh4_read32(Sh4Context* c, uint32_t a) {
    if (sh4_is_ram(a) && !(a & 3)) {
        uint32_t v;
        std::memcpy(&v, c->ram + (a & kRamMask), 4);
        return v;
    }
    return sh4_read32_slow(c, a);
}
inline void sh4_write8(Sh4Context* c, uint32_t a, uint32_t v) {
#if BS_WRITE_WATCH
    if (sh4_is_ram(a)) { const uint8_t b = uint8_t(v); ww_ram(c->ram + (a & kRamMask), a & kRamMask, &b, 1); }
    if (sh4_is_ram(a) && (g_wwWriter.on || !g_wwWriter.init)) ww_writer(c, a & kRamMask, uint8_t(v), 1);
#endif
    if (sh4_is_ram(a)) c->ram[a & kRamMask] = uint8_t(v);
    else sh4_write8_slow(c, a, uint8_t(v));
}
inline void sh4_write16(Sh4Context* c, uint32_t a, uint32_t v) {
    if (sh4_is_ram(a) && !(a & 1)) {
        const uint16_t w = uint16_t(v);
#if BS_WRITE_WATCH
        ww_ram(c->ram + (a & kRamMask), a & kRamMask, &w, 2);
        if (g_wwWriter.on || !g_wwWriter.init) ww_writer(c, a & kRamMask, w, 2);
#endif
        std::memcpy(c->ram + (a & kRamMask), &w, 2);
    } else {
        sh4_write16_slow(c, a, uint16_t(v));
    }
}
inline void sh4_write32(Sh4Context* c, uint32_t a, uint32_t v) {
    if (sh4_is_ram(a) && !(a & 3)) {
#if BS_WRITE_WATCH
        ww_ram(c->ram + (a & kRamMask), a & kRamMask, &v, 4);
        if (g_wwWriter.on || !g_wwWriter.init) ww_writer(c, a & kRamMask, v, 4);
#endif
        std::memcpy(c->ram + (a & kRamMask), &v, 4);
    } else {
        sh4_write32_slow(c, a, v);
    }
}

} // namespace bs
