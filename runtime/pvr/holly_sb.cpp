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

#include "maple/maple.h"
#include "memory/sh4_dmac.h"
#include "pvr/holly_intc.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace bs {
namespace {

constexpr uint32_t kBase  = 0x005F6800;
constexpr uint32_t kSize  = 0x1800;
constexpr uint32_t kSbRev = 0x005F689C;
constexpr uint32_t kC2Dstat = 0x005F6800, kC2Dlen = 0x005F6804, kC2Dst = 0x005F6808;
constexpr uint32_t kIstPvrDma = 1u << 19;

std::array<uint32_t, kSize / 4> g_regs{};
std::array<bool, kSize / 4>     g_reported{};

uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }

} // namespace

void holly_sb_reset() {
    g_regs.fill(0);
    g_reported.fill(false);
    maple_reset();
}

bool holly_sb_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    if (maple_read(c, a, v)) return true;
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

bool holly_sb_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    if (maple_write(c, a, v)) return true;
    if (p != kSbRev) g_regs[(p - kBase) / 4] = v;
    // Diagnostico: BS_PVR_DEBUG=1 registra o disparo do DMA do canal 2 (SB_C2DST)
    static const bool debug = std::getenv("BS_PVR_DEBUG") != nullptr;
    static int        n     = 0;
    if (debug && p == 0x005F6808 && n++ < 20)
        std::fprintf(stderr, "[sb] C2DST=%08X C2DSTAT=%08X C2DLEN=%08X\n", v, g_regs[0], g_regs[1]);
    // Diagnostico: BS_SB_LOG=1 registra as escritas nos registradores de DMA do G2
    // (0x005F7800-0x005F78FF: AICA, ext1, ext2, dev), ainda sem modelo
    static const bool g2log = std::getenv("BS_SB_LOG") != nullptr;
    static int        g2n   = 0;
    if (g2log && p >= 0x005F7800 && p < 0x005F7900 && g2n++ < 400)
        std::fprintf(stderr, "[sb] G2 escrita %08X = %08X (ciclo %llu)\n", p, v, (unsigned long long)c->cycles);
    // SB_C2DST = 1: DMA do canal 2 (SH-4 DMAC) de SB_C2DLEN bytes para SB_C2DSTAT. No fim,
    // C2DSTAT avanca, C2DLEN e C2DST zeram e ISTNRM bit 19 (fim do DMA do PVR) acende;
    // a interrupcao e avaliada no proximo poll (next_event = 0).
    if (p == kC2Dst && (v & 1)) {
        uint32_t& dest = g_regs[(kC2Dstat - kBase) / 4];
        uint32_t& len  = g_regs[(kC2Dlen - kBase) / 4];
        if (sh4_dmac_ch2_transfer(c, dest, len)) {
            dest += len;
            len = 0;
            g_regs[(kC2Dst - kBase) / 4] = 0;
            holly_intc_raise(kIstPvrDma);
            c->next_event = 0;
        }
    }
    return true;
}

} // namespace bs
