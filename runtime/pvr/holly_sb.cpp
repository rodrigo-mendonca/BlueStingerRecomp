// System block do Holly (documentacao do KallistiOS, referencia de comportamento).
//   SB_SBREV 0x005F689C: revisao do barramento, 0x0000000B (so leitura; valor lido
//                        pelo boot do jogo em 8C1C41DC no Flycast)
// O controlador de interrupcoes (0x005F6900-0x005F693F) fica em holly_intc.cpp e e
// consultado antes deste arquivo.
//
// Registrador sem modelo proprio devolve o ultimo valor escrito. A primeira leitura
// de cada um e registrada: um registrador de status que precise de comportamento
// real (DMA em andamento, Maple) aparece aqui antes de virar divergencia.
#include "pvr/holly_sb.h"

#include <array>
#include <cstdio>

namespace bs {
namespace {

constexpr uint32_t kBase  = 0x005F6800;
constexpr uint32_t kSize  = 0x1800;
constexpr uint32_t kSbRev = 0x005F689C;

std::array<uint32_t, kSize / 4> g_regs{};
std::array<bool, kSize / 4>     g_reported{};

uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }

} // namespace

void holly_sb_reset() {
    g_regs.fill(0);
    g_reported.fill(false);
}

bool holly_sb_read(Sh4Context*, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    if (p == kSbRev) {
        v = 0x0000000B;
        return true;
    }
    const uint32_t i = (p - kBase) / 4;
    v                = g_regs[i];
    if (!g_reported[i]) {
        g_reported[i] = true;
        std::fprintf(stderr, "[sb] leitura sem modelo: %08X -> %08X (ultimo valor escrito)\n", p, v);
    }
    return true;
}

bool holly_sb_write(Sh4Context*, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    if (p != kSbRev) g_regs[(p - kBase) / 4] = v;
    return true;
}

} // namespace bs
