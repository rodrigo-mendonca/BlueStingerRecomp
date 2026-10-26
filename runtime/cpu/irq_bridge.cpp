#include "cpu/irq_bridge.h"

#include "cpu/sh4_intc.h"
#include "cpu/sh4_runtime.h"
#include "cpu/time_hooks.h"
#include "debug/event_trace.h"
#include "debug/intc_trace.h"
#include "pvr/holly_intc.h"

#include <cstdio>
#include <cstdlib>

namespace bs {
namespace {

constexpr uint32_t kIcr = 0xFFD00000, kIpra = 0xFFD00004, kIprc = 0xFFD0000C;
constexpr uint32_t kIntevt = 0xFF000028;

struct State {
    uint16_t icr = 0, ipr[3] = {0, 0, 0};
    uint32_t intevt  = 0;
    bool     adopted = false;  // o modelo assumiu (fora do contrato)
    uint64_t taken = 0, fallbacks = 0, served = 0;
} g;

void enter(Sh4Context* c, uint32_t intevt) {
    g.intevt = intevt;
    ++g.taken;
    const uint32_t sr = sh4_get_sr(*c);
    if (g_intc_trace) intc_trace_delivery(sh4_irq_delivery_origin(), 0, intevt, c->pc, sr);
    if (g_event_trace) event_trace(TraceEvent::Irq, c->cycles, intevt, c->pc);
    c->ssr = sr;
    c->spc = c->pc;
    c->sgr = c->r[15];
    static const bool debug = std::getenv("BS_IRQ_DEBUG") != nullptr;
    if (debug)
        std::fprintf(stderr, "[irq] aceita intevt=%03X spc=%08X ssr=%08X r15=%08X pr=%08X\n", intevt, c->spc, sr,
                     c->r[15], c->pr);
    sh4_set_sr(*c, sr | kSrMD | kSrRB | kSrBL);
    const uint32_t saved = c->ret_expect, savedSp = c->ret_sp, savedFn = c->cur_fn;
    c->ret_expect        = c->spc;  // o rte do tratador volta para SPC (BS_RTE confere)
    c->ret_sp            = c->sgr;  // com a pilha da thread interrompida
    c->cur_fn            = 0;       // sonda: o despacho do tratador vem do runtime
    sh4_dispatch(c, c->spc, c->vbr + 0x600);
    c->ret_expect = saved;
    c->ret_sp     = savedSp;
    c->cur_fn     = savedFn;
}

bool deliverOne(Sh4Context* c) {
    const uint32_t sr = sh4_get_sr(*c);
    if (sr & kSrBL) return false;
    const uint32_t imask = (sr & kSrImask) >> 4;
    const uint32_t level = holly_irl_level(time_now(c));
    if (level <= imask) return false;
    enter(c, 0x200 + (15 - level) * 0x20);
    return true;
}

bool handOver() {
    if (!g.adopted) {
        sh4_intc_adopt(g.icr, g.ipr, g.intevt);
        g.adopted = true;
    }
    ++g.fallbacks;
    return false;
}

} // namespace

bool irq_bridge_active() { return !g.adopted && !time_provider().cycle_cadence(); }

void irq_bridge_check(Sh4Context* c) {
    for (int n = 0; deliverOne(c); ++n) {
        // o tratador saiu do contrato (entrega ao modelo): o modelo reavalia e entrega o resto, com o INTEVT dele
        // (revisao Codex 2026-10-26)
        if (g.adopted) return sh4_irq_check(c);
        if (n == 1000) sh4_fatal(c, "interrupcao nunca reconhecida pelo tratador", c->pc, g.intevt);
    }
}

bool irq_bridge_read(uint32_t a, unsigned size, uint32_t& v) {
    const bool reg = a == kIntevt || a == kIcr || (a >= kIpra && a <= kIprc + 1);
    if (!reg) return false;
    if (g.adopted) return ++g.fallbacks, false;
    if (time_provider().cycle_cadence()) return false;
    if (a == kIntevt && size == 4) {
        v = g.intevt;
        ++g.served;
        return true;
    }
    return handOver();
}

bool irq_bridge_write(uint32_t a, unsigned size, uint32_t v) {
    const bool reg = a == kIntevt || a == kIcr || (a >= kIpra && a <= kIprc + 1);
    if (!reg) return false;
    if (g.adopted) return ++g.fallbacks, false;
    if (time_provider().cycle_cadence()) return false;
    if (size == 2 && a == kIcr) g.icr = uint16_t(v);
    else if (size == 2 && a >= kIpra && a <= kIprc && !(a & 3)) g.ipr[(a - kIpra) / 4] = uint16_t(v);
    else return handOver();
    ++g.served;
    return true;
}

void irq_bridge_reset() {
    const uint64_t t = g.taken, f = g.fallbacks, s = g.served;
    g           = State{};
    g.taken     = t;  // os contadores atravessam sessoes, como o g_taken do modelo
    g.fallbacks = f;
    g.served    = s;
}

uint64_t irq_bridge_taken() { return g.taken; }
uint64_t irq_bridge_fallbacks() { return g.fallbacks; }
uint64_t irq_bridge_served() { return g.served; }

} // namespace bs
