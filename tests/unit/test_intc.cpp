// Entrega de interrupcoes (runtime/cpu/sh4_intc.cpp) e RTC do AICA.
#include "test.h"

#include "aica/aica_regs.h"
#include "cpu/sh4_intc.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "cpu/time_hooks.h"
#include "pvr/holly_intc.h"
#include "pvr/pvr_regs.h"

#include <vector>

using namespace bs;

namespace {

constexpr uint32_t kVbr = 0x8C00F400;
constexpr uint64_t kField = 200000000ull * 1001 / 60000;

struct Seen {
    int      calls = 0;
    uint32_t sr = 0, ssr = 0, spc = 0, sgr = 0, intevt = 0, r0 = 0;
} g_seen;

// Tratador falso em VBR+0x600: registra o estado e faz o que o rte gerado faz.
void handler(Sh4Context* c) {
    ++g_seen.calls;
    g_seen.sr     = sh4_get_sr(*c);
    g_seen.ssr    = c->ssr;
    g_seen.spc    = c->spc;
    g_seen.sgr    = c->sgr;
    g_seen.r0     = c->r[0];
    g_seen.intevt = sh4_read32(c, 0xFF000028);
    sh4_write32(c, 0xA05F6900, kIstVblankIn);  // reconhece (escrever 1 limpa)
    const uint32_t tgt = c->spc;
    sh4_set_sr(*c, c->ssr);
    BS_CHECK(tgt == c->ret_expect);
}

const Sh4FuncEntry kTable[] = {{(kVbr + 0x600) & 0x1FFFFFFF, handler}};  // endereco fisico

struct IrqFixture {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    IrqFixture() {
        c.ram = ram.data();
        c.vbr = kVbr;
        sh4_set_function_table(kTable, 1);
        sh4_intc_reset();
        pvr_regs_reset();
        holly_intc_reset();
        g_seen = {};
        sh4_write32(&c, 0xA05F80D8, (262u << 16) | 857);  // SPG_LOAD: 263 linhas
        sh4_write32(&c, 0xA05F80CC, (21u << 16) | 240);   // VBlank-In na linha 240
        sh4_write32(&c, 0xA05F6930, kIstVblankIn);         // IML6NRM: VBlank-In no nivel 6
        c.cycles = kField * 241 / 263;                      // depois da linha 240
    }
};

} // namespace

// SR com IMASK = 0: o nivel 6 pendente e aceito; entrada conforme o manual SH-4.
BS_TEST(intc_irl6_vblank_is_delivered_to_vbr_600) {
    IrqFixture f;
    sh4_set_sr(f.c, 0x40000000);  // MD, banco 0, IMASK 0
    f.c.r[0]  = 0x11111111;       // banco 0
    f.c.rbank[0] = 0x22222222;    // banco 1
    f.c.r[15] = 0x8C00F3A0;
    f.c.pc    = 0x8C08BBB4;       // retomada (8C08BBB2: ldc r0,sr no boot do jogo)
    f.c.ret_expect = 0x8C000000;
    sh4_irq_check(&f.c);
    BS_CHECK(g_seen.calls == 1);
    BS_CHECK(g_seen.ssr == 0x40000000 && g_seen.spc == 0x8C08BBB4 && g_seen.sgr == 0x8C00F3A0);
    BS_CHECK(g_seen.intevt == 0x320);                               // 0x200 + (15 - 6) * 0x20
    BS_CHECK((g_seen.sr & 0x70000000) == 0x70000000);              // MD, RB e BL ligados
    BS_CHECK(g_seen.r0 == 0x22222222);                              // RB=1: banco 1 visivel
    BS_CHECK(sh4_get_sr(f.c) == 0x40000000 && f.c.r[0] == 0x11111111);  // rte restaurou
    BS_CHECK(f.c.ret_expect == 0x8C000000);
    sh4_irq_check(&f.c);  // reconhecido: nada pendente
    BS_CHECK(g_seen.calls == 1);
}

BS_TEST(intc_masked_or_blocked_interrupt_is_not_delivered) {
    IrqFixture f;
    sh4_set_sr(f.c, 0x40000060);  // IMASK = 6: nivel 6 nao e maior
    sh4_irq_check(&f.c);
    BS_CHECK(g_seen.calls == 0);
    sh4_set_sr(f.c, 0x50000000);  // BL = 1
    sh4_irq_check(&f.c);
    BS_CHECK(g_seen.calls == 0);
    sh4_set_sr(f.c, 0x40000050);  // IMASK = 5
    f.c.pc = 0x8C010000;
    sh4_irq_check(&f.c);
    BS_CHECK(g_seen.calls == 1);
}

// Boot do Blue Stinger: le o RTC em 0xA0710000/04 (segundos desde 1950, 16+16 bits).
BS_TEST(aica_rtc_counts_emulated_seconds_and_accepts_enabled_writes) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    aica_rtc_set(0x906D38C5, 0);
    auto rtc = [&] { return (sh4_read32(&c, 0xA0710000) << 16) | sh4_read32(&c, 0xA0710004); };
    BS_CHECK(rtc() == 0x906D38C5);
    c.cycles = 3 * 200000000ull + 5;
    BS_CHECK(rtc() == 0x906D38C8);
    sh4_write32(&c, 0xA0710004, 0x1234);  // sem liberar: ignorado
    BS_CHECK(rtc() == 0x906D38C8);
    sh4_write32(&c, 0xA0710008, 1);
    sh4_write32(&c, 0xA0710004, 0x0000);
    sh4_write32(&c, 0xA0710000, 0x1000);  // a parte alta desliga a liberacao
    BS_CHECK(rtc() == 0x10000000);
    sh4_write32(&c, 0xA0710000, 0x2000);
    BS_CHECK(rtc() == 0x10000000);
    BS_CHECK(aica_rtc_host_now() > 0x80000000u);  // depois de 2018 na epoca de 1950
}

// N8.1, cadencia B (tempo logico): quem deixa um nivel entregavel fora de um atendimento pede o atendimento no
// proximo poll (next_event = 0); no tempo estavel nada muda (o quantum de ciclos continua). Revisao Codex 2026-10-25.
namespace {
struct LogicalSpy : TimeProvider {
    const char* name() const override { return "logico"; }
    void        reset(uint64_t) override {}
    uint64_t    clock(uint64_t cycles) override { return cycles; }
    uint32_t    holly_due(uint64_t) override { return 0; }
    void        spg_changed() override {}
    void        render_scheduled(uint64_t) override {}
    void        maple_scheduled(uint64_t) override {}
    void        maple_cancel() override {}
    void        disc_scheduled(uint64_t) override {}
    bool        disc_due(uint64_t) override { return false; }
    void        disc_cancel() override {}
    uint32_t    rtc_seconds(uint64_t) override { return 0; }
    void        rtc_set(uint32_t, uint64_t) override {}
    bool        cycle_cadence() const override { return false; }
    uint64_t    progress_calls = 0;
    void        progress(uint32_t, uint64_t, uint32_t) override { ++progress_calls; }
};
} // namespace

BS_TEST(n8_cadence_b_wakeup_requests_service_only_when_deliverable) {
    IrqFixture f;  // IML6NRM = VBlank-In
    LogicalSpy spy;
    time_set_provider(&spy);
    sh4_irq_bind(&f.c);
    sh4_set_sr(f.c, 0x400000F0);  // IMASK 15: mascarado pelo SR
    f.c.next_event = ~0ull;
    holly_intc_raise(kIstVblankIn);
    BS_CHECK(f.c.next_event == ~0ull);  // nada entregavel: nenhum pedido
    sh4_write32(&f.c, 0xA05F6930, 0);      // IML6NRM = 0
    sh4_set_sr(f.c, 0x40000000);           // IMASK 0 (no jogo o ldc sr passa pelo BS_IRQ_CHECK)
    BS_CHECK(f.c.next_event == ~0ull);
    sh4_write32(&f.c, 0xA05F6930, kIstVblankIn);  // a mascara deixa o nivel 6 entregavel
    BS_CHECK(f.c.next_event == 0);
    f.c.next_event = ~0ull;
    sh4_write32(&f.c, 0xA05F6900, kIstVblankIn);  // reconhece
    holly_intc_raise(kIstVblankIn);               // fonte nova ja entregavel
    BS_CHECK(f.c.next_event == 0);
    f.c.next_event = ~0ull;
    sh4_request_service();
    BS_CHECK(f.c.next_event == 0);
    // vivacidade: tick a cada kProgressTick polls, so o progresso
    f.c.polls = 10, f.c.next_poll = 0;
    sh4_progress_tick(&f.c);
    BS_CHECK(f.c.next_poll == 10 + kProgressTick && spy.progress_calls == 1 && g_seen.calls == 0);
    // reset: o contexto da sessao anterior nao e mais escrito
    sh4_intc_reset();
    f.c.next_event = ~0ull;
    holly_intc_raise(kIstVblankIn);
    sh4_request_service();
    BS_CHECK(f.c.next_event == ~0ull);
    time_set_provider(nullptr);
}

BS_TEST(n8_cadence_b_stable_time_keeps_the_cycle_quantum) {
    IrqFixture f;  // provedor legado (cadencia por ciclos)
    sh4_irq_bind(&f.c);
    sh4_set_sr(f.c, 0x40000000);
    f.c.next_event = ~0ull;
    holly_intc_raise(kIstVblankIn);
    sh4_request_service();
    BS_CHECK(f.c.next_event == ~0ull);  // no estavel nada pede atendimento: o quantum continua
    f.c.next_poll = 0;
    sh4_progress_tick(&f.c);
    BS_CHECK(f.c.next_poll == ~0ull);  // contexto zerado: o tick so se desliga
    f.c.cycles = 1000, f.c.next_event = 0, f.c.pc = 0x8C010000;
    sh4_service(&f.c);
    BS_CHECK(f.c.next_event == 1000 + kServiceQuantum);
    sh4_intc_reset();
}
