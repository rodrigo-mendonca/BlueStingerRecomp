#include "cpu/sh4_context.h"

#include <cfenv>
#include <xmmintrin.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace bs {

void sh4_reset(Sh4Context& c, uint32_t entry) {
    uint8_t* ram   = c.ram;
    void*    owner = c.owner;
    std::memset(&c, 0, sizeof(c));
    c.ram   = ram;
    c.owner = owner;
    c.sr    = 0x700000F0;  // MD=1 RB=1 BL=1 IMASK=0xF (estado de reset)
    c.pc    = entry;
    sh4_set_fpscr(c, 0x00040001);  // valor de reset: DN=1, RM=01 (para zero)
    // M4: estado pos-BIOS (pilha, VBR, SR) definido pelo HLE de boot.
}

uint32_t sh4_get_sr(const Sh4Context& c) { return c.sr | (c.t & 1); }

// Banco efetivo de R0-R7: BANK1 so em modo privilegiado (MD=1) com RB=1.
static bool bank1(uint32_t sr) { return (sr & kSrMD) && (sr & kSrRB); }

void sh4_set_sr(Sh4Context& c, uint32_t v) {
    v &= kSrMask;
    if (bank1(v) != bank1(c.sr)) {
        for (int i = 0; i < 8; ++i) {
            const uint32_t tmp = c.r[i];
            c.r[i]             = c.rbank[i];
            c.rbank[i]         = tmp;
        }
    }
    c.t  = v & kSrT;
    c.sr = v & ~kSrT;
}

void sh4_set_fpscr(Sh4Context& c, uint32_t v) {
    v &= kFpscrMask;
    if ((v ^ c.fpscr) & kFpscrFR) {
        uint32_t tmp[16];
        std::memcpy(tmp, c.fr, sizeof(tmp));
        std::memcpy(c.fr, c.xf, sizeof(tmp));
        std::memcpy(c.xf, tmp, sizeof(tmp));
    }
    c.fpscr = v;
    // RM: 00 = mais proximo, 01 = para zero (padrao do Dreamcast). O host segue o guest.
    std::fesetround((v & kFpscrRM) == 1 ? FE_TOWARDZERO : FE_TONEAREST);
    // DN=1: denormalizados viram zero na entrada e no resultado (FTZ + DAZ no MXCSR).
    constexpr unsigned kFtzDaz = 0x8040;
    _mm_setcsr((v & kFpscrDN) ? (_mm_getcsr() | kFtzDaz) : (_mm_getcsr() & ~kFtzDaz));
}

const float* sh4_fsca_table() {
    static const std::vector<float> table = [] {
        const int saved = std::fegetround();
        std::fesetround(FE_TONEAREST);
        std::vector<float> t(0x20000);
        for (uint32_t k = 0; k < 0x10000; ++k) {
            const double ang = double(k) * (6.283185307179586476925 / 65536.0);
            t[2 * k]         = float(std::sin(ang));
            t[2 * k + 1]     = float(std::cos(ang));
        }
        std::fesetround(saved);
        return t;
    }();
    return table.data();
}

} // namespace bs
