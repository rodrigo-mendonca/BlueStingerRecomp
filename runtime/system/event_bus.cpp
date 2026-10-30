// N8.7 (docs/N8-7-KAMUI.md, D-K3 e incremento 4a): no produto, o controlador de interrupcoes do Holly (pvr/holly_intc.h)
// e o banco minimo de status nativo (native/render/kamui/event_status), alimentado pelos eventos que o provedor de tempo
// entrega nos pontos de sempre (acesso, poll, nivel IRL). O tratador original do Kamui le e limpa o banco como antes. O
// controlador do legado (pvr/holly_intc.cpp) fica na validacao como referencia.
#include "pvr/holly_intc.h"

#include "cpu/sh4_intc.h"
#include "cpu/time_hooks.h"
#include "debug/clock_log.h"
#include "debug/event_trace.h"
#include "debug/intc_trace.h"
#include "pvr/pvr_regs.h"
#include "pvr/pvr_video.h"
#include "render/kamui/event_status.h"
#include "system/maple_bus.h"
#include "system/graphics_ports.h"

#include <cstdio>
#include <cstdlib>

namespace bs {
namespace {

using native::render::kamui::EventStatus;

EventStatus g_bank;
uint64_t    g_last     = 0;
uint64_t    g_vblankAt = 0;  // instante do ultimo VBlank-In entregue (diagnostico BS_TMU_DIAG)

uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }
uint32_t rg(uint32_t p) { return g_bank.reg(p); }

void update(uint64_t now) {
    // no tempo experimental (N2b-2) o instante fica parado entre dois atendimentos: um evento vencido no mesmo instante
    // ainda e entregue
    if (now < g_last || (now == g_last && !time_provider().holly_pending(now))) return;
    const uint32_t   due = time_provider().holly_due(now);
    EventStatus::Due d;
    d.vblank_in = due & kDueVblankIn, d.vblank_out = due & kDueVblankOut, d.render = due & kDueRender;
    d.maple            = due & kDueMaple;
    const uint32_t before = rg(EventStatus::kIstNrm);
    g_bank.deliver(d);
    if (d.vblank_in) g_vblankAt = now;
    if (d.vblank_in && g_event_trace) event_trace(TraceEvent::VBlank, now, uint32_t(g_bank.vblanks()));
    if (d.vblank_in && g_clock_log) clock_log_vblank(g_bank.vblanks());
    if (d.render) pvr_render_done_take();
    if (d.maple) periph_dma_take();
    g_last = now;
    if (due && g_intc_trace) {
        const uint32_t asked = (d.vblank_in ? kIstVblankIn : 0) | (d.vblank_out ? kIstVblankOut : 0) |
                               (d.render ? 0x7u : 0) | (d.maple ? kIstMapleDma : 0);
        intc_trace_holly('H', IntcSource::Update, now, rg(EventStatus::kIstNrm) & ~before, asked,
                         rg(EventStatus::kIstNrm), rg(0x005F6930), rg(0x005F6920), rg(0x005F6910));
    }
    // host (apresentacao de quadros, ritmo): depois de atualizar o estado, porque o gancho pode interromper a execucao
    if (d.vblank_in) pvr_video_vblank();
}

} // namespace

void holly_intc_sync(uint64_t cycles) { update(cycles); }

uint64_t holly_vblank_count() { return g_bank.vblanks(); }
uint64_t holly_last_vblank_instant() { return g_vblankAt; }
uint64_t holly_vblank_fused() { return g_bank.fused(); }
uint64_t holly_event_count() { return g_bank.events(); }
std::string holly_event_status_summary() { return {}; }  // a comparacao com o controlador do legado so na validacao

void event_status_raise(uint32_t bits) {
    const uint32_t before = rg(EventStatus::kIstNrm);
    g_bank.raise(bits);
    sh4_irq_wakeup();  // N8.1, cadencia B (no tempo estavel nao faz nada)
    if (g_intc_trace)
        intc_trace_holly('H', IntcSource::Raise, g_last, rg(EventStatus::kIstNrm) & ~before, bits,
                         rg(EventStatus::kIstNrm), rg(0x005F6930), rg(0x005F6920), rg(0x005F6910));
}

void holly_intc_peek(uint32_t& istnrm, uint32_t& iml6nrm, uint32_t& iml4nrm, uint32_t& iml2nrm) {
    istnrm = rg(EventStatus::kIstNrm), iml6nrm = rg(0x005F6930), iml4nrm = rg(0x005F6920), iml2nrm = rg(0x005F6910);
}

uint32_t holly_irl_level(uint64_t cycles) {
    update(cycles);
    return holly_irl_level_peek();
}

uint32_t holly_irl_level_peek() { return g_bank.irl_level(); }

void holly_intc_reset(uint64_t cycles) {
    g_bank.reset();
    g_last     = cycles;
    g_vblankAt = cycles;
    time_provider().reset(cycles);
}

bool holly_intc_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (!EventStatus::contains(p) || size != 4) return false;
    update(time_now(c));
    v = rg(p);
    const bool ist = p == EventStatus::kIstNrm || p == EventStatus::kIstExt || p == EventStatus::kIstErr;
    if (g_clock_log && ist) clock_log_read(ClockRead::Ist, v);
    if (g_intc_trace && ist) intc_trace_clock(IntcSource::Ist, v);
    return true;
}

bool holly_intc_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (!EventStatus::contains(p) || size != 4) return false;
    const uint64_t now = time_now(c);
    update(now);
    if (p == EventStatus::kIstNrm || p == EventStatus::kIstExt || p == EventStatus::kIstErr) {
        const uint32_t before = rg(p);
        g_bank.write(p, v);  // escrever 1 limpa
        if (g_intc_trace)
            intc_trace_holly('A',
                             p == EventStatus::kIstNrm   ? IntcSource::IstNrm
                             : p == EventStatus::kIstExt ? IntcSource::IstExt
                                                         : IntcSource::IstErr,
                             now, before & v, v, rg(EventStatus::kIstNrm), rg(0x005F6930), rg(0x005F6920),
                             rg(0x005F6910));
    } else {
        // Diagnostico: BS_PVR_DEBUG=1 registra mudancas das mascaras IML*
        static const bool debug = std::getenv("BS_PVR_DEBUG") != nullptr;
        if (debug && rg(p) != v) std::fprintf(stderr, "[intc] %08X = %08X\n", p, v);
        g_bank.write(p, v);
        sh4_irq_wakeup();  // N8.1, cadencia B: mascara nova pode deixar um nivel entregavel
    }
    return true;
}

} // namespace bs
