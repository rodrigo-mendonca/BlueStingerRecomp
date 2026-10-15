// Registradores (documentacao do KallistiOS, referencia de comportamento):
//   ISTNRM 0x005F6900: status normal; escrever 1 limpa o bit. Bit 3 VBlank-In,
//                      bit 4 VBlank-Out.
//   ISTEXT 0x005F6904, ISTERR 0x005F6908: status externo/erro (ainda sem fontes).
//   IML2NRM..IML6ERR 0x005F6910-0x005F6938: mascaras por nivel (so guardadas).
//
// Os eventos sao avaliados de forma preguicosa: a cada acesso, os cruzamentos de
// linha entre o ultimo acesso e `c->cycles` acendem os bits correspondentes.
#include "pvr/holly_intc.h"
#include "debug/event_trace.h"

#include "pvr/pvr_regs.h"
#include "maple/maple.h"
#include "pvr/pvr_video.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace bs {
namespace {

constexpr uint32_t kBase   = 0x005F6900;
constexpr uint32_t kSize   = 0x40;
constexpr uint32_t kIstNrm = 0x005F6900;
constexpr uint32_t kIstExt = 0x005F6904;
constexpr uint32_t kIstErr = 0x005F6908;

std::array<uint32_t, kSize / 4> g_regs{};
uint64_t                        g_last = 0;

uint32_t& reg(uint32_t a) { return g_regs[(a - kBase) / 4]; }
uint32_t  canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }

uint64_t g_vblanks = 0;  // contagem de VBlank-In (trace de ordem)

void update(uint64_t now) {
    if (now <= g_last) return;
    const bool vblankIn = pvr_spg_crossed(g_last, now, pvr_vblank_in_line());
    if (vblankIn) reg(kIstNrm) |= kIstVblankIn;
    if (vblankIn && g_event_trace) event_trace(TraceEvent::VBlank, now, uint32_t(++g_vblanks));
    if (pvr_spg_crossed(g_last, now, pvr_vblank_out_line())) reg(kIstNrm) |= kIstVblankOut;
    // fim de render agendado pelo STARTRENDER: bits 0 (video), 1 (ISP), 2 (TSP)
    if (const uint64_t due = pvr_render_done_due(); due && now >= due) {
        reg(kIstNrm) |= 0x7;
        pvr_render_done_take();
    }
    // fim do DMA do Maple: bit 12
    if (const uint64_t due = maple_dma_due(); due && now >= due) {
        reg(kIstNrm) |= kIstMapleDma;
        maple_dma_take();
    }
    g_last = now;
    // host (apresentacao de quadros, ritmo): depois de atualizar o estado, porque o
    // gancho pode interromper a execucao (encerramento)
    if (vblankIn) pvr_video_vblank();
}

} // namespace

void holly_intc_sync(uint64_t cycles) { update(cycles); }

void holly_intc_raise(uint32_t bits) { reg(kIstNrm) |= bits; }

uint32_t holly_irl_level(uint64_t cycles) {
    update(cycles);
    const uint32_t nrm = reg(kIstNrm), ext = reg(kIstExt), err = reg(kIstErr);
    // IML6NRM 0x005F6930, IML4NRM 0x005F6920, IML2NRM 0x005F6910; EXT +4, ERR +8
    for (uint32_t level : {6u, 4u, 2u}) {
        const uint32_t m = 0x005F6910 + (level / 2 - 1) * 0x10;
        if ((nrm & reg(m)) || (ext & reg(m + 4)) || (err & reg(m + 8))) return level;
    }
    return 0;
}

void holly_intc_reset(uint64_t cycles) {
    g_regs.fill(0);
    g_last    = cycles;
    g_vblanks = 0;
}

bool holly_intc_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    update(c->cycles);
    v = reg(p);
    return true;
}

bool holly_intc_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    update(c->cycles);
    if (p == kIstNrm || p == kIstExt || p == kIstErr) {
        reg(p) &= ~v;  // escrever 1 limpa
    } else {
        // Diagnostico: BS_PVR_DEBUG=1 registra mudancas das mascaras IML*
        static const bool debug = std::getenv("BS_PVR_DEBUG") != nullptr;
        if (debug && reg(p) != v) std::fprintf(stderr, "[intc] %08X = %08X\n", p, v);
        reg(p) = v;
    }
    return true;
}

} // namespace bs
