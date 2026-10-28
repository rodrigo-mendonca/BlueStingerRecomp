// Entrega de interrupcoes (runtime/cpu/sh4_intc.cpp) e RTC do AICA.
#include "test.h"

#include "aica/aica_regs.h"
#include "cpu/sh4_intc.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "cpu/time_hooks.h"
#include "memory/sh4_tmu.h"
#include "memory/tmu_logical.h"
#include "memory/sh4_ccn.h"
#include "memory/sh4_port.h"
#include "memory/sys_logical.h"
#include "cpu/irq_bridge.h"
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
// N8.6: a area de som nativa (o produto) e o modelo da AICA (validacao) se comportam igual.
BS_TEST(aica_rtc_counts_emulated_seconds_and_accepts_enabled_writes) {
  for (const SoundBusBackend* bk : {static_cast<const SoundBusBackend*>(nullptr), aica_bus_backend()}) {
    snd_bus_use(bk);
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    snd_bus_rtc_set(0x906D38C5, 0);
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
    BS_CHECK(aica_rtc_host_now() > 0x80000000u && snd_rtc_host_now() > 0x80000000u);  // depois de 2018 (epoca de 1950)
    snd_bus_use(nullptr);
  }
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

// N8.1: o servico do TMU no tempo logico (memory/tmu_logical) da o mesmo TCNT0 que o modelo para o contrato do jogo
// (inicializacao de 8C15D17C) e entrega o estado ao modelo no primeiro acesso fora dele.
namespace {
void tmuInit(Sh4Context& c) {  // a sequencia de 8C15D17C
    sh4_write8(&c, 0xFFD80000, 0);
    sh4_write8(&c, 0xFFD80004, sh4_read8(&c, 0xFFD80004) & 0xFE);
    sh4_write16(&c, 0xFFD80010, 2);
    sh4_write32(&c, 0xFFD80008, 0xFFFFFFFF);
    sh4_write32(&c, 0xFFD8000C, 0xFFFFFFFF);
    sh4_write8(&c, 0xFFD80004, sh4_read8(&c, 0xFFD80004) | 1);
}
} // namespace

BS_TEST(n8_tmu_logical_matches_the_model_and_hands_over_outside_the_contract) {
    std::vector<uint8_t> ram(16u << 20);
    // o modelo (tempo estavel: provedor legado, quantum de ciclos)
    Sh4Context m{};
    m.ram = ram.data();
    sh4_tmu_reset();
    tmu_logical_reset();
    m.cycles = 1000;
    tmuInit(m);
    const uint64_t served0 = tmu_logical_served();
    std::vector<uint32_t> model;
    for (uint64_t t : {1000ull, 1255ull, 1256ull, 1000 + 256 * 777ull, 1000 + 256 * 123456ull})
        m.cycles = t, model.push_back(sh4_read32(&m, 0xFFD8000C));
    BS_CHECK(tmu_logical_served() == served0);  // no estavel o servico nao atende
    // o servico (tempo logico), mesma sequencia e mesmos instantes
    LogicalSpy spy;
    time_set_provider(&spy);
    Sh4Context c{};
    c.ram = ram.data();
    sh4_tmu_reset();
    tmu_logical_reset();
    c.cycles = 1000;
    tmuInit(c);
    std::vector<uint32_t> logical;
    for (uint64_t t : {1000ull, 1255ull, 1256ull, 1000 + 256 * 777ull, 1000 + 256 * 123456ull})
        c.cycles = t, logical.push_back(sh4_read32(&c, 0xFFD8000C));
    BS_CHECK(logical == model);
    BS_CHECK(logical[2] == 0xFFFFFFFEu && logical[3] == 0xFFFFFFFFu - 777);
    BS_CHECK(tmu_logical_served() > served0 && tmu_logical_fallbacks() == 0);
    // fora do contrato (leitura do TCR0): o modelo assume com o mesmo canal 0, e o TCNT0 continua sem salto
    c.cycles = 1000 + 256 * 200000ull;
    BS_CHECK(sh4_read16(&c, 0xFFD80010) == 2);
    BS_CHECK(tmu_logical_fallbacks() == 1);
    c.cycles = 1000 + 256 * 200001ull;
    BS_CHECK(sh4_read32(&c, 0xFFD8000C) == 0xFFFFFFFFu - 200001);
    BS_CHECK(tmu_logical_fallbacks() == 2);
    time_set_provider(nullptr);
    sh4_tmu_reset();
    tmu_logical_reset();
}

// Revisao Codex 2026-10-25: servico x modelo numa sequencia com underflow e recarga, TCR regravado depois do avanco
// (limpa UNF), parada e reinicio por TSTR, entrega ao modelo por uma escrita fora do contrato, espelho da area 7 e
// reset entre sessoes. Toda leitura tem de ser a mesma nos dois.
namespace {
struct TmuOp {
    uint64_t t;
    char     op;  // r/w = leitura/escrita; i = inicializacao (8C15D17C); x = reset de sessao
    uint32_t a;
    unsigned size;
    uint32_t v;
};
std::vector<uint32_t> tmuRun(const std::vector<TmuOp>& ops) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    sh4_tmu_reset();
    tmu_logical_reset();
    std::vector<uint32_t> out;
    for (const TmuOp& o : ops) {
        c.cycles = o.t;
        if (o.op == 'i') tmuInit(c);
        else if (o.op == 'x') sh4_tmu_reset(), tmu_logical_reset();
        else if (o.op == 'w')
            o.size == 1 ? sh4_write8(&c, o.a, uint8_t(o.v)) : o.size == 2 ? sh4_write16(&c, o.a, uint16_t(o.v)) : sh4_write32(&c, o.a, o.v);
        else out.push_back(o.size == 1 ? sh4_read8(&c, o.a) : o.size == 2 ? sh4_read16(&c, o.a) : sh4_read32(&c, o.a));
    }
    return out;
}
} // namespace

BS_TEST(n8_tmu_logical_sequences_match_the_model) {
    constexpr uint64_t K = 256, W = 0x100000000ull * K;  // um tick do canal; uma volta inteira do TCNT0
    const std::vector<std::vector<TmuOp>> seqs = {
        // underflow e recarga, sem sair do contrato
        {{100, 'i'}, {100 + W - K, 'r', 0xFFD8000C, 4}, {100 + W, 'r', 0xFFD8000C, 4}, {100 + W + 5 * K, 'r', 0xFFD8000C, 4},
         {100 + 3 * W + 7 * K, 'r', 0xFFD8000C, 4}},
        // TCR0 regravado com o mesmo divisor depois do avanco e do underflow (rebase; UNF limpo), depois TSTR para e volta
        {{0, 'i'}, {W + 9 * K, 'r', 0xFFD8000C, 4}, {W + 10 * K, 'w', 0xFFD80010, 2, 2}, {W + 30 * K, 'r', 0xFFD8000C, 4},
         {W + 31 * K, 'w', 0xFFD80004, 1, 0}, {W + 900 * K, 'r', 0xFFD8000C, 4}, {W + 901 * K, 'r', 0xFFD80004, 1},
         {W + 902 * K, 'w', 0xFFD80004, 1, 1}, {W + 1000 * K, 'r', 0xFFD8000C, 4}},
        // escrita fora do contrato (outro divisor): o modelo assume no meio, sem salto
        {{0, 'i'}, {50 * K, 'r', 0xFFD8000C, 4}, {60 * K, 'w', 0xFFD80010, 2, 3}, {60 * K + 1024 * 7, 'r', 0xFFD8000C, 4},
         {60 * K + 1024 * 9, 'r', 0xFFD80010, 2}},
        // espelho da area 7: o mesmo TMU (entregue ao modelo, mesmo estado)
        {{0, 'i'}, {70 * K, 'r', 0x1FD8000C, 4}, {80 * K, 'r', 0xFFD8000C, 4}, {90 * K, 'r', 0x1FD80004, 1}},
        // duas sessoes: o reset volta ao estado inicial nos dois
        {{0, 'i'}, {40 * K, 'r', 0xFFD8000C, 4}, {41 * K, 'x'}, {50 * K, 'r', 0xFFD8000C, 4}, {60 * K, 'i'},
         {70 * K, 'r', 0xFFD8000C, 4}},
    };
    LogicalSpy spy;
    for (size_t i = 0; i < seqs.size(); ++i) {
        const auto model = tmuRun(seqs[i]);  // tempo estavel: so o modelo
        time_set_provider(&spy);
        const auto logical = tmuRun(seqs[i]);  // tempo logico: servico (e o modelo depois da entrega)
        time_set_provider(nullptr);
        BS_CHECK(!model.empty() && logical == model);
    }
    sh4_tmu_reset();
    tmu_logical_reset();
}

// N8.1: CCR, arrays do cache e PDTRA no tempo logico (memory/sys_logical) = os modelos, inclusive depois de sair do
// contrato (PCTRA, CCR com 16 bits, array com 8 bits, espelho da area 7) e entre duas sessoes (revisao Codex 2026-10-26)
namespace {
std::vector<uint32_t> sysRun() {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    sh4_ccn_reset();
    sh4_port_reset();
    sys_logical_reset();
    std::vector<uint32_t> out;
    sh4_write32(&c, 0xFF00001C, 0x0000090D);   // CCR com ICI e OCI: so leem 0
    out.push_back(sh4_read32(&c, 0xFF00001C));
    sh4_write32(&c, 0xF4001234, 0xDEADBEEF);   // array de enderecos do OC: sem efeito
    out.push_back(sh4_read32(&c, 0xF4001234));
    out.push_back(sh4_read16(&c, 0xFF800030));  // PDTRA: o cabo nos pinos 8-9
    sh4_write32(&c, 0xFF80002C, 0x00000005);   // PCTRA: pinos 0 e 1 saida (fora do contrato)
    sh4_write16(&c, 0xFF800030, 0x0003);
    out.push_back(sh4_read16(&c, 0xFF800030));
    out.push_back(sh4_read32(&c, 0xFF80002C));
    out.push_back(sh4_read8(&c, 0xF4000010));  // array com 8 bits (fora do contrato)
    out.push_back(sh4_read16(&c, 0xFF00001C) | 0x10000u);  // CCR com 16 bits (fora do contrato)
    sh4_write32(&c, 0x1F00001C, 0x00000021);   // CCR pelo espelho da area 7
    out.push_back(sh4_read32(&c, 0xFF00001C));
    // sessao nova: estado inicial de novo nos dois caminhos
    sh4_ccn_reset();
    sh4_port_reset();
    sys_logical_reset();
    out.push_back(sh4_read32(&c, 0xFF00001C));
    out.push_back(sh4_read16(&c, 0xFF800030));
    return out;
}
} // namespace

BS_TEST(n8_ccn_and_cable_logical_match_the_models) {
    const uint64_t s0 = sys_logical_served(), f0 = sys_logical_fallbacks();
    const auto     model = sysRun();  // tempo estavel: so os modelos
    BS_CHECK(sys_logical_served() == s0 && sys_logical_fallbacks() == f0);
    LogicalSpy spy;
    time_set_provider(&spy);
    const auto logical = sysRun();
    time_set_provider(nullptr);
    BS_CHECK(logical == model);
    BS_CHECK(model[0] == 0x00000105 && model[1] == 0 && model[2] == 0x0300 && model[7] == 0x21);  // composto = 3
    BS_CHECK(model[8] == 0 && model[9] == 0x0300);  // reset da sessao
    const uint64_t s1 = sys_logical_served(), f1 = sys_logical_fallbacks();
    BS_CHECK(s1 > s0 && f1 > f0);
    sys_logical_reset();  // os contadores atravessam sessoes (diagnostico do processo)
    BS_CHECK(sys_logical_served() == s1 && sys_logical_fallbacks() == f1);
    sh4_ccn_reset();
    sh4_port_reset();
}

// N8.1: a ponte de interrupcoes do tempo logico (cpu/irq_bridge) entrega como o modelo: o tratador ve o mesmo SR, SSR,
// SPC, SGR, INTEVT e banco; mascarada ou bloqueada, nada; fora do contrato (leitura de IPR) o modelo assume.
namespace {
Seen irqOnce(bool logical) {
    IrqFixture f;
    LogicalSpy spy;
    if (logical) time_set_provider(&spy);
    irq_bridge_reset();
    sh4_set_sr(f.c, 0x40000000);
    f.c.r[0] = 0x11111111, f.c.rbank[0] = 0x22222222, f.c.r[15] = 0x8C00F3A0, f.c.pc = 0x8C08BBB4;
    f.c.ret_expect = 0x8C000000;
    holly_intc_raise(kIstVblankIn);  // a mesma fonte nos dois (o espiao nao agenda VBlank)
    sh4_irq_check(&f.c);
    const Seen s = g_seen;
    sh4_set_sr(f.c, 0x40000060);  // IMASK 6: nada
    sh4_irq_check(&f.c);
    BS_CHECK(g_seen.calls == s.calls);
    time_set_provider(nullptr);
    return s;
}
} // namespace

BS_TEST(n8_irq_bridge_delivers_like_the_model) {
    const uint64_t t0 = sh4_irq_taken_count(), b0 = irq_bridge_taken();
    const Seen     m  = irqOnce(false);
    BS_CHECK(irq_bridge_taken() == b0);  // no estavel a ponte nao entrega
    const Seen l = irqOnce(true);
    BS_CHECK(irq_bridge_taken() == b0 + 1 && sh4_irq_taken_count() == t0 + 2);
    BS_CHECK(m.calls == 1 && l.calls == 1);
    BS_CHECK(l.sr == m.sr && l.ssr == m.ssr && l.spc == m.spc && l.sgr == m.sgr && l.intevt == m.intevt && l.r0 == m.r0);
    BS_CHECK(l.intevt == 0x320 && l.spc == 0x8C08BBB4);
    // fora do contrato: leitura de IPRA (16 bits) entrega o estado ao modelo
    {
        IrqFixture f;
        LogicalSpy spy;
        time_set_provider(&spy);
        irq_bridge_reset();
        sh4_write16(&f.c, 0xFFD00004, 0x1234);  // IPRA: no contrato (escrita)
        const uint64_t fb = irq_bridge_fallbacks();
        BS_CHECK(sh4_read16(&f.c, 0xFFD00004) == 0x1234);
        BS_CHECK(irq_bridge_fallbacks() == fb + 1 && !irq_bridge_active());
        time_set_provider(nullptr);
        irq_bridge_reset();
        sh4_intc_reset();
    }
}

// Revisao Codex 2026-10-26: o primeiro tratador sai do contrato (le IPRA) e reconhece o nivel 6; o nivel 4 ainda
// pendente tem de ser entregue pelo modelo, e o segundo tratador le INTEVT = 0x360.
namespace {
std::vector<uint32_t> g_intevts;
void handlerOut(Sh4Context* c) {
    g_intevts.push_back(sh4_read32(c, 0xFF000028));
    if (g_intevts.size() == 1) {
        (void)sh4_read16(c, 0xFFD00004);           // fora do contrato: entrega ao modelo
        sh4_write32(c, 0xA05F6900, kIstVblankIn);  // reconhece o nivel 6
    } else {
        sh4_write32(c, 0xA05F6900, 1u << 12);      // reconhece o nivel 4
    }
    sh4_set_sr(*c, c->ssr);
}
const Sh4FuncEntry kTableOut[] = {{(kVbr + 0x600) & 0x1FFFFFFF, handlerOut}};
} // namespace

BS_TEST(n8_irq_bridge_hands_over_inside_a_handler) {
    IrqFixture f;  // IML6NRM = VBlank-In
    LogicalSpy spy;
    time_set_provider(&spy);
    irq_bridge_reset();
    sh4_set_function_table(kTableOut, 1);
    g_intevts.clear();
    sh4_write32(&f.c, 0xA05F6920, 1u << 12);  // IML4NRM = fim do DMA do Maple
    holly_intc_raise(kIstVblankIn);
    holly_intc_raise(1u << 12);
    sh4_set_sr(f.c, 0x40000000);
    f.c.pc = 0x8C08BBB4, f.c.ret_expect = 0x8C000000;
    sh4_irq_check(&f.c);
    BS_CHECK(g_intevts.size() == 2 && g_intevts[0] == 0x320 && g_intevts[1] == 0x360);
    BS_CHECK(!irq_bridge_active());
    time_set_provider(nullptr);
    sh4_set_function_table(kTable, 1);
    irq_bridge_reset();
    sh4_intc_reset();
}
