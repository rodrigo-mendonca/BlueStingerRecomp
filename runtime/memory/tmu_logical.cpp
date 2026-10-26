#include "memory/tmu_logical.h"

#include "cpu/time_hooks.h"
#include "debug/clock_log.h"
#include "debug/intc_trace.h"
#include "memory/sh4_tmu.h"

#include <cstdlib>
#include <cstring>

namespace bs {
namespace {

constexpr uint32_t kTocr = 0xFFD80000, kTstr = 0xFFD80004, kTcor0 = 0xFFD80008, kTcnt0 = 0xFFD8000C, kTcr0 = 0xFFD80010;
constexpr uint32_t kEnd    = 0xFFD8002F;  // ultimo byte do TMU (TCPR2)
constexpr uint32_t kTcrP64 = 2;           // TPSC = P/64, sem UNIE
constexpr uint64_t kDiv    = 4 * 64;      // ticks logicos (200 MHz) por tick do canal 0 em P/64
constexpr uint32_t kUnf    = 1u << 8;

// O canal 0 do contrato, com a mesma semantica do modelo (base no instante baseAt; TSTR e TCR fixam a base).
struct State {
    uint8_t  tocr = 0, tstr = 0;
    uint32_t tcr = 0, tcor = 0xFFFFFFFF, base = 0xFFFFFFFF;
    uint64_t baseAt   = 0;
    bool     adopted  = false;  // o modelo assumiu (acesso fora do contrato)
    uint64_t fallbacks = 0, served = 0;
} g;

bool diagOn() {
    static const bool on = [] {
        const char* v = std::getenv("BS_TMU_DIAG");
        return v && *v;
    }();
    return on;
}
bool stopped() {
    static const bool on = [] {
        const char* v = std::getenv("BS_TMU_MUTACAO");
        return v && std::strcmp(v, "parado") == 0;
    }();
    return on;
}

bool active() { return !g.adopted && !diagOn() && !time_provider().cycle_cadence(); }

uint32_t count(uint64_t now) {
    if (!(g.tstr & 1) || now <= g.baseAt || stopped()) return g.base;
    const uint64_t ticks = (now - g.baseAt) / kDiv;
    if (ticks <= g.base) return g.base - uint32_t(ticks);
    g.tcr |= kUnf;
    const uint64_t period = uint64_t(g.tcor) + 1;
    return g.tcor - uint32_t((ticks - g.base - 1) % period);
}
void rebase(uint64_t now) {
    g.base   = count(now);
    g.baseAt = now;
}

// fora do contrato: o modelo recebe o estado do canal 0 neste instante e atende daqui em diante
bool handOver(Sh4Context* c) {
    if (!g.adopted) {
        const uint64_t now = time_now(c);
        rebase(now);
        sh4_tmu_adopt(g.tocr, g.tstr, g.tcor, g.tcr, g.base, now);
        g.adopted = true;
    }
    ++g.fallbacks;
    return false;
}

} // namespace

// area 7 (0x1FD80000) e o mesmo TMU que o P4; fora do contrato (o jogo so usa o P4): entregue ao modelo
static bool mirror(uint32_t a) { return a >= 0x1FD80000 && a <= 0x1FD8002F; }

bool tmu_logical_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    if (mirror(a)) return active() || g.adopted ? handOver(c) : false;
    if (a < kTocr || a > kEnd) return false;
    if (g.adopted) return ++g.fallbacks, false;
    if (!active()) return false;
    if (a == kTcnt0 && size == 4) {
        v = count(time_now(c));
        if (g_clock_log) clock_log_read(ClockRead::Tmu, v);
        if (g_intc_trace) intc_trace_clock(IntcSource::Tmu, v);
    } else if (a == kTstr && size == 1) {
        v = g.tstr;
    } else {
        return handOver(c);
    }
    ++g.served;
    return true;
}

bool tmu_logical_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    if (mirror(a)) return active() || g.adopted ? handOver(c) : false;
    if (a < kTocr || a > kEnd) return false;
    if (g.adopted) return ++g.fallbacks, false;
    if (!active()) return false;
    const uint64_t now = time_now(c);
    if (a == kTocr && size == 1 && (v & 1) == 0) {
        g.tocr = 0;
    } else if (a == kTstr && size == 1 && (v & 6) == 0) {  // os canais 1 e 2 nunca partem no contrato
        rebase(now);
        g.tstr = uint8_t(v & 1);
    } else if (a == kTcr0 && size == 2 && (v & 0x3FF) == kTcrP64) {
        rebase(now);
        g.tcr = (v & ~0x300u & 0x3FF) | (g.tcr & (v | ~0x300u) & 0x300u);  // UNF so pode ser limpo
    } else if (a == kTcor0 && size == 4 && v == 0xFFFFFFFF) {
        g.tcor = v;
    } else if (a == kTcnt0 && size == 4 && v == 0xFFFFFFFF) {
        g.base   = v;
        g.baseAt = now;
    } else {
        return handOver(c);
    }
    ++g.served;
    return true;
}

void tmu_logical_reset() {
    const uint64_t f = g.fallbacks, s = g.served;
    g             = State{};
    g.fallbacks   = f;  // os contadores atravessam sessoes (diagnostico do processo)
    g.served      = s;
}

uint64_t tmu_logical_state_hash() {
    uint64_t       h   = 1469598103934665603ull;
    const uint64_t f[] = {g.tocr, g.tstr, g.tcr, g.tcor, g.base, g.baseAt, g.adopted};
    for (uint64_t x : f) h = (h ^ x) * 1099511628211ull;
    return h;
}

uint64_t tmu_logical_fallbacks() { return g.fallbacks; }
uint64_t tmu_logical_served() { return g.served; }

} // namespace bs
