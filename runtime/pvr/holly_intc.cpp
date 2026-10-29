// Registradores (documentacao do KallistiOS, referencia de comportamento):
//   ISTNRM 0x005F6900: status normal; escrever 1 limpa o bit. Bit 3 VBlank-In,
//                      bit 4 VBlank-Out.
//   ISTEXT 0x005F6904, ISTERR 0x005F6908: status externo/erro (ainda sem fontes).
//   IML2NRM..IML6ERR 0x005F6910-0x005F6938: mascaras por nivel (so guardadas).
//
// Os eventos sao avaliados nos pontos de sempre (acesso, poll, nivel IRL): o provedor de tempo
// (cpu/time_hooks.h) diz o que venceu desde o ultimo ponto.
#include "pvr/holly_intc.h"
#include "cpu/sh4_intc.h"
#include "cpu/time_hooks.h"
#include "debug/clock_log.h"
#include "debug/event_trace.h"
#include "debug/intc_trace.h"

#include "pvr/pvr_regs.h"
#include "system/maple_bus.h"
#include "pvr/pvr_video.h"
#include "render/kamui/event_status.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

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
uint64_t g_vblankAt = 0;  // instante do ultimo VBlank-In entregue (diagnostico BS_TMU_DIAG)
uint64_t g_events = 0;  // eventos do Holly entregues (diagnostico N2b: epoca, mesmo mascarados)
uint64_t g_vblankFused = 0;  // VBlank-In com o bit ainda aceso: um atendimento para dois (N2b, B4)

// N8.7 4a (docs/N8-7-KAMUI.md, D-K3): este controlador e a referencia; o banco de status nativo (o do produto) roda em
// sombra, recebe os mesmos eventos e escritas e e comparado depois de cada operacao (os 16 registradores, o nivel IRL e
// as contagens). Negativo (BS_EVENT_STATUS_MUTACAO=limpa): a sombra ignora a limpeza do bit 19 (fim do DMA do canal 2)
native::render::kamui::EventStatus g_shadow;
uint64_t                           g_checks = 0, g_differ = 0;
std::string                        g_first;

bool mutation_noclear() {
    static const bool m = [] {
        const char* v = std::getenv("BS_EVENT_STATUS_MUTACAO");
        return v && std::string(v) == "limpa";
    }();
    return m;
}

void shadow_check(const char* what) {
    ++g_checks;
    bool same = g_shadow.vblanks() == g_vblanks && g_shadow.fused() == g_vblankFused && g_shadow.events() == g_events;
    for (uint32_t i = 0; i < kSize / 4 && same; ++i) same = g_shadow.reg(kBase + 4 * i) == g_regs[i];
    if (same) {
        // o nivel IRL com a conta do controlador (holly_irl_level_peek, abaixo)
        const uint32_t nrm = g_regs[0], ext = g_regs[1], err = g_regs[2];
        uint32_t       lvl = 0;
        for (uint32_t level : {6u, 4u, 2u}) {
            const uint32_t m = (0x10 + (level / 2 - 1) * 0x10) / 4;
            if ((nrm & g_regs[m]) || (ext & g_regs[m + 1]) || (err & g_regs[m + 2])) {
                lvl = level;
                break;
            }
        }
        same = g_shadow.irl_level() == lvl;
    }
    if (same) return;
    ++g_differ;
    if (g_first.empty()) {
        char b[160];
        std::snprintf(b, sizeof b, "%s (operacao %llu): ISTNRM legado %08X, nativo %08X; VBlanks %llu/%llu", what,
                      (unsigned long long)g_checks, g_regs[0], g_shadow.reg(kIstNrm), (unsigned long long)g_vblanks,
                      (unsigned long long)g_shadow.vblanks());
        g_first = b;
    }
}

void update(uint64_t now) {
    // no tempo experimental (N2b-2) o instante fica parado entre dois atendimentos: um evento
    // vencido no mesmo instante ainda e entregue
    if (now < g_last || (now == g_last && !time_provider().holly_pending(now))) return;
    // N2a: os eventos agendados vencidos vem do provedor de tempo (legado: calculo preguicoso
    // por ciclos; nativo: fila de eventos), entregues aqui como antes
    const uint32_t due      = time_provider().holly_due(now);
    const bool     vblankIn = due & kDueVblankIn;
    const uint32_t before   = reg(kIstNrm);  // N8.1: trace H (bits pedidos x acesos agora)
    if (due) ++g_events;
    if (vblankIn && (reg(kIstNrm) & kIstVblankIn)) ++g_vblankFused;
    if (vblankIn) reg(kIstNrm) |= kIstVblankIn;
    if (vblankIn) ++g_vblanks, g_vblankAt = now;
    if (vblankIn && g_event_trace) event_trace(TraceEvent::VBlank, now, uint32_t(g_vblanks));
    if (vblankIn && g_clock_log) clock_log_vblank(g_vblanks);
    if (due & kDueVblankOut) reg(kIstNrm) |= kIstVblankOut;
    // fim de render agendado pelo STARTRENDER: bits 0 (video), 1 (ISP), 2 (TSP)
    if (due & kDueRender) {
        reg(kIstNrm) |= 0x7;
        pvr_render_done_take();
    }
    // fim do DMA do Maple: bit 12
    if (due & kDueMaple) {
        reg(kIstNrm) |= kIstMapleDma;
        periph_dma_take();
    }
    g_last = now;
    if (due) {
        native::render::kamui::EventStatus::Due d;
        d.vblank_in = vblankIn, d.vblank_out = due & kDueVblankOut, d.render = due & kDueRender, d.maple = due & kDueMaple;
        g_shadow.deliver(d);
        shadow_check("eventos vencidos");
    }
    if (due && g_intc_trace) {
        const uint32_t asked = (vblankIn ? kIstVblankIn : 0) | (due & kDueVblankOut ? kIstVblankOut : 0) |
                               (due & kDueRender ? 0x7u : 0) | (due & kDueMaple ? kIstMapleDma : 0);
        intc_trace_holly('H', IntcSource::Update, now, reg(kIstNrm) & ~before, asked, reg(kIstNrm), reg(0x005F6930),
                         reg(0x005F6920), reg(0x005F6910));
    }
    // host (apresentacao de quadros, ritmo): depois de atualizar o estado, porque o
    // gancho pode interromper a execucao (encerramento)
    if (vblankIn) pvr_video_vblank();
}

} // namespace

void holly_intc_sync(uint64_t cycles) { update(cycles); }

uint64_t holly_vblank_count() { return g_vblanks; }
uint64_t holly_last_vblank_instant() { return g_vblankAt; }
uint64_t holly_vblank_fused() { return g_vblankFused; }

void holly_intc_raise(uint32_t bits) {
    const uint32_t before = reg(kIstNrm);
    reg(kIstNrm) |= bits;
    ++g_events;
    g_shadow.raise(bits);
    shadow_check("evento levantado");
    sh4_irq_wakeup();  // N8.1, cadencia B (no tempo estavel nao faz nada)
    if (g_intc_trace)
        intc_trace_holly('H', IntcSource::Raise, g_last, reg(kIstNrm) & ~before, bits, reg(kIstNrm), reg(0x005F6930),
                         reg(0x005F6920), reg(0x005F6910));
}
uint64_t holly_event_count() { return g_events; }

std::string holly_event_status_summary() {
    return "[event-status] operacoes comparadas " + std::to_string(g_checks) + ", diferentes " + std::to_string(g_differ) +
           (g_first.empty() ? std::string() : "; primeira: " + g_first) + "\n";
}
void     holly_intc_peek(uint32_t& istnrm, uint32_t& iml6nrm, uint32_t& iml4nrm, uint32_t& iml2nrm) {
    istnrm = reg(kIstNrm), iml6nrm = reg(0x005F6930), iml4nrm = reg(0x005F6920), iml2nrm = reg(0x005F6910);
}

uint32_t holly_irl_level(uint64_t cycles) {
    update(cycles);
    return holly_irl_level_peek();
}

uint32_t holly_irl_level_peek() {
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
    g_vblankAt = cycles;
    g_vblankFused = 0;
    g_shadow.reset();
    time_provider().reset(cycles);
}

bool holly_intc_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    update(time_now(c));
    v = reg(p);
    if (g_clock_log && (p == kIstNrm || p == kIstExt || p == kIstErr)) clock_log_read(ClockRead::Ist, v);
    if (g_intc_trace && (p == kIstNrm || p == kIstExt || p == kIstErr)) intc_trace_clock(IntcSource::Ist, v);
    return true;
}

bool holly_intc_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    const uint64_t now = time_now(c);
    update(now);
    if (p == kIstNrm || p == kIstExt || p == kIstErr) {
        const uint32_t before = reg(p);
        reg(p) &= ~v;  // escrever 1 limpa
        g_shadow.write(p, mutation_noclear() && p == kIstNrm ? v & ~(1u << 19) : v);
        shadow_check("limpeza");
        if (g_intc_trace)
            intc_trace_holly('A', p == kIstNrm ? IntcSource::IstNrm : p == kIstExt ? IntcSource::IstExt : IntcSource::IstErr,
                             now, before & v, v, reg(kIstNrm), reg(0x005F6930), reg(0x005F6920), reg(0x005F6910));
    } else {
        // Diagnostico: BS_PVR_DEBUG=1 registra mudancas das mascaras IML*
        static const bool debug = std::getenv("BS_PVR_DEBUG") != nullptr;
        if (debug && reg(p) != v) std::fprintf(stderr, "[intc] %08X = %08X\n", p, v);
        reg(p) = v;
        g_shadow.write(p, v);
        shadow_check("mascara");
        sh4_irq_wakeup();  // N8.1, cadencia B: mascara nova pode deixar um nivel entregavel
    }
    return true;
}

} // namespace bs
