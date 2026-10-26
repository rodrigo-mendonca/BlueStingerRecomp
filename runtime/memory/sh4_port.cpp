// Porta A do SH-4 (manual SH7750, cap. 20 "I/O Ports"):
//   PCTRA 0xFF80002C (32 bits): por pino n, bit 2n = 1 -> saida, 0 -> entrada.
//   PDTRA 0xFF800030 (16 bits): pinos de saida devolvem o valor escrito; pinos de
//                               entrada devolvem o nivel externo.
// Dreamcast: os pinos 8-9 sao entradas ligadas ao tipo de cabo de video. Os demais
// pinos de entrada ainda nao tem evidencia de uso e leem 0 (registrado uma vez).
#include "memory/sh4_port.h"

#include <cstdio>

namespace bs {
namespace {

constexpr uint32_t kPctra = 0x1F80002C;  // endereco sem espelho (P4 e area 7)
constexpr uint32_t kPdtra = 0x1F800030;
constexpr uint16_t kCablePins = 0x0300;

uint32_t   g_pctra = 0;
uint16_t   g_pdtra = 0;
VideoCable g_cable = VideoCable::Composite;

uint32_t canon(uint32_t a) {
    // P4 (0xFF800000) e area 7 (0x1F800000) enxergam os mesmos registradores
    return (a >= 0xE0000000 || (a >> 29) == 0) ? (a & 0x1FFFFFFF) : 0;
}

uint16_t outputMask() {
    uint16_t m = 0;
    for (int n = 0; n < 16; ++n)
        if (g_pctra & (1u << (2 * n))) m |= uint16_t(1u << n);
    return m;
}

} // namespace

void       sh4_port_set_cable(VideoCable c) { g_cable = c; }
VideoCable sh4_port_cable() { return g_cable; }
void       sh4_port_reset() { g_pctra = 0; g_pdtra = 0; }
void       sh4_port_adopt(uint32_t pctra, uint16_t pdtra) { g_pctra = pctra, g_pdtra = pdtra; }

bool sh4_port_read(uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (p == kPctra && size == 4) {
        v = g_pctra;
        return true;
    }
    if (p == kPdtra && size == 2) {
        const uint16_t out = outputMask();
        const uint16_t in  = uint16_t(~out & kCablePins & (uint32_t(g_cable) << 8));
        static bool warned = false;
        if (!warned && uint16_t(~out & ~kCablePins)) {
            warned = true;
            std::fprintf(stderr, "[port] PDTRA: pinos de entrada fora do cabo leem 0 (PCTRA=%08X)\n", g_pctra);
        }
        v = uint16_t((g_pdtra & out) | in);
        return true;
    }
    return false;
}

bool sh4_port_write(uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (p == kPctra && size == 4) {
        g_pctra = v;
        return true;
    }
    if (p == kPdtra && size == 2) {
        g_pdtra = uint16_t(v);
        return true;
    }
    return false;
}

} // namespace bs
