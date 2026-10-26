// INTC (manual SH7750, cap. 19 "Interrupt Controller"; cap. 5 "Exceptions"):
//   ICR 0xFFD00000, IPRA/IPRB/IPRC 0xFFD00004/08/0C (16 bits): guardados.
//   INTEVT 0xFF000028: codigo da ultima interrupcao aceita.
//   Aceitacao: SR.BL = 0 e prioridade > SR.IMASK. Entrada: SSR = SR, SPC = PC de
//   retomada, SGR = R15, SR.MD = SR.RB = SR.BL = 1, PC = VBR + 0x600.
// IRL do Dreamcast: o Holly sinaliza os niveis 6, 4 e 2 (mascaras IML6/4/2), que o
// SH-4 recebe como interrupcoes IRL de prioridade 6/4/2 e INTEVT 0x320/0x360/0x3A0
// (0x200 + (15 - nivel) * 0x20).
#include "cpu/sh4_intc.h"
#include "debug/event_trace.h"
#include "debug/intc_trace.h"

#include "cpu/sh4_runtime.h"
#include "cpu/sh4_threads.h"
#include "cpu/time_hooks.h"
#include "pvr/holly_intc.h"

#include <cstdio>
#include <cstdlib>

namespace bs {
namespace {

constexpr uint32_t kIcr = 0xFFD00000, kIpra = 0xFFD00004, kIprb = 0xFFD00008, kIprc = 0xFFD0000C;
constexpr uint32_t kIntevt = 0xFF000028;

uint16_t g_icr = 0, g_ipr[3] = {0, 0, 0};
uint32_t g_intevt = 0;
uint64_t g_taken  = 0;  // diagnostico (sh4_irq_taken_count)
// diagnostico BS_INTC_TRACE: origem do sh4_service em curso, por thread do guest (cada fibra tem a sua pilha: uma troca de
// thread dentro de um atendimento nao pode rotular as entregas da outra); profundidade 0 = fora de sh4_service
struct Origin {
    IntcSource src   = IntcSource::IrqCheck;
    int        depth = 0;
};
constexpr int kOriginSlots = 8;
Origin        g_origins[kOriginSlots];
IntcSource    g_hint = IntcSource::Cadence;  // o proximo sh4_service vem de onde (sh4_irq_origin_hint)
// N8.1 (cadencia B): o contexto da CPU (um so por sessao; sh4_irq_bind no boot, renovado por sh4_irq_check e zerado
// no reset: o contexto de uma sessao encerrada nao pode ser escrito)
Sh4Context* g_ctxLast = nullptr;
Origin&       origin() {
    const int t = sh4_current_thread();
    return g_origins[t >= 0 && t < kOriginSlots ? t : kOriginSlots - 1];
}

void take(Sh4Context* c, uint32_t intevt) {
    g_intevt     = intevt;
    ++g_taken;
    const uint32_t sr = sh4_get_sr(*c);
    if (g_intc_trace) intc_trace_delivery(origin().depth ? origin().src : IntcSource::IrqCheck, 0, intevt, c->pc, sr);
    if (g_event_trace) event_trace(TraceEvent::Irq, c->cycles, intevt, c->pc);
    c->ssr       = sr;
    c->spc       = c->pc;
    c->sgr       = c->r[15];
    if (std::getenv("BS_IRQ_DEBUG"))
        std::fprintf(stderr, "[irq] aceita intevt=%03X spc=%08X ssr=%08X r15=%08X pr=%08X\n", intevt, c->spc, sr,
                     c->r[15], c->pr);
    sh4_set_sr(*c, sr | kSrMD | kSrRB | kSrBL);
    const uint32_t saved = c->ret_expect, savedSp = c->ret_sp, savedFn = c->cur_fn;
    c->ret_expect        = c->spc;  // o rte do tratador volta para SPC (BS_RTE confere)
    c->ret_sp            = c->sgr;  // com a pilha da thread interrompida
    c->cur_fn            = 0;       // sonda: o despacho do tratador vem do runtime, nao da funcao interrompida
    sh4_dispatch(c, c->spc, c->vbr + 0x600);
    c->ret_expect = saved;
    c->ret_sp     = savedSp;
    c->cur_fn     = savedFn;
}

// Entrega a interrupcao pendente de maior prioridade, se SR permitir.
bool deliverOne(Sh4Context* c) {
    const uint32_t sr = sh4_get_sr(*c);
    if (sr & kSrBL) return false;
    const uint32_t imask = (sr & kSrImask) >> 4;
    const uint32_t level = holly_irl_level(time_now(c));
    // Diagnostico: BS_IRQ_DEBUG=1 lista as primeiras checagens com nivel pendente.
    static const bool debug = std::getenv("BS_IRQ_DEBUG") != nullptr;
    static int        dbg   = 0;
    if (debug && dbg < 40 && (level || (dbg < 20 && imask < 15))) {
        ++dbg;
        std::fprintf(stderr, "[irq] pc=%08X sr=%08X imask=%u level=%u cycles=%llu\n", c->pc, sr, imask, level,
                     (unsigned long long)c->cycles);
    }
    if (level <= imask) return false;
    take(c, 0x200 + (15 - level) * 0x20);
    return true;
}

} // namespace

uint64_t sh4_irq_taken_count() { return g_taken; }

void sh4_irq_origin_hint(uint8_t origin) { g_hint = IntcSource(origin); }

// Marca a origem das entregas feitas dentro de sh4_service (e restaura no fim: o tratador pode chamar outro).
IntcSource sh4_irq_origin_enter(const Sh4Context* c) {
    Origin&          o    = origin();
    const IntcSource prev = o.src;
    o.src                 = c->next_event == 0 ? IntcSource::Forced : g_hint;
    g_hint                = IntcSource::Cadence;
    ++o.depth;
    return prev;
}
void sh4_irq_origin_leave(IntcSource prev) {
    Origin& o = origin();
    o.src     = prev;
    if (o.depth) --o.depth;
}
uint8_t sh4_irq_origin_now() { return origin().depth ? uint8_t(origin().src) : 0; }

// o trace H pergunta a origem por aqui (debug/ nao depende de cpu/)
static const bool kOriginFn = (intc_trace_set_origin_fn(sh4_irq_origin_now), true);

void sh4_intc_reset() {
    // diagnostico: sessao nova comeca sem origem aberta (uma fibra apagada no encerramento nao desenrola o RAII de
    // sh4_service; revisao Codex 2026-10-25)
    for (Origin& o : g_origins) o = Origin{};
    g_hint    = IntcSource::Cadence;
    g_ctxLast = nullptr;
    g_icr = 0;
    g_ipr[0] = g_ipr[1] = g_ipr[2] = 0;
    g_intevt = 0;
}

void sh4_irq_bind(Sh4Context* c) { g_ctxLast = c; }

void sh4_irq_wakeup() {
    Sh4Context* c = g_ctxLast;
    if (!c || time_provider().cycle_cadence()) return;
    const uint32_t sr = sh4_get_sr(*c);
    if (!(sr & kSrBL) && holly_irl_level_peek() > ((sr & kSrImask) >> 4)) c->next_event = 0;
}

void sh4_request_service() {
    if (g_ctxLast && !time_provider().cycle_cadence()) g_ctxLast->next_event = 0;
}

void sh4_irq_check(Sh4Context* c) {
    g_ctxLast = c;
    // Depois do rte do tratador, uma fonte ainda pendente (ou outra) e aceita antes da
    // instrucao em SPC (manual SH7750, 19.6): por isso reavalia ate nao haver nada.
    for (int n = 0; deliverOne(c); ++n)
        if (n == 1000) sh4_fatal(c, "interrupcao nunca reconhecida pelo tratador", c->pc, g_intevt);
}

bool sh4_intc_read(uint32_t a, unsigned size, uint32_t& v) {
    if (a == kIntevt && size == 4) {
        v = g_intevt;
        return true;
    }
    if (size != 2) return false;
    if (a == kIcr) v = g_icr;
    else if (a >= kIpra && a <= kIprc && !(a & 3)) v = g_ipr[(a - kIpra) / 4];
    else return false;
    return true;
}

bool sh4_intc_write(uint32_t a, unsigned size, uint32_t v) {
    if (a == kIntevt && size == 4) {
        g_intevt = v;
        return true;
    }
    if (size != 2) return false;
    if (a == kIcr) g_icr = uint16_t(v);
    else if (a >= kIpra && a <= kIprc && !(a & 3)) g_ipr[(a - kIpra) / 4] = uint16_t(v);
    else return false;
    return true;
}

} // namespace bs
