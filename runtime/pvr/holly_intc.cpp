// Registradores (documentacao do KallistiOS, referencia de comportamento):
//   ISTNRM 0x005F6900: status normal; escrever 1 limpa o bit. Bit 3 VBlank-In,
//                      bit 4 VBlank-Out.
//   ISTEXT 0x005F6904, ISTERR 0x005F6908: status externo/erro (ainda sem fontes).
//   IML2NRM..IML6ERR 0x005F6910-0x005F6938: mascaras por nivel (so guardadas).
//
// Os eventos sao avaliados de forma preguicosa: a cada acesso, os cruzamentos de
// linha entre o ultimo acesso e `c->cycles` acendem os bits correspondentes.
#include "pvr/holly_intc.h"

#include "pvr/pvr_regs.h"

#include <array>

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

void update(uint64_t now) {
    if (now <= g_last) return;
    if (pvr_spg_crossed(g_last, now, pvr_vblank_in_line())) reg(kIstNrm) |= kIstVblankIn;
    if (pvr_spg_crossed(g_last, now, pvr_vblank_out_line())) reg(kIstNrm) |= kIstVblankOut;
    g_last = now;
}

} // namespace

void holly_intc_sync(uint64_t cycles) { update(cycles); }

void holly_intc_reset(uint64_t cycles) {
    g_regs.fill(0);
    g_last = cycles;
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
    if (p == kIstNrm || p == kIstExt || p == kIstErr) reg(p) &= ~v;  // escrever 1 limpa
    else reg(p) = v;
    return true;
}

} // namespace bs
