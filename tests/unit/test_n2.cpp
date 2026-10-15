// N2a: relogio logico e fila de eventos nativos (native/platform) e o provedor de tempo
// nativo (transitional/time_adapter) contra o legado (runtime/cpu/time_legacy.cpp).
#include "test.h"

#include "aica/aica_regs.h"
#include "cpu/sh4_mem.h"
#include "cpu/time_hooks.h"
#include "gdrom/gdrom.h"
#include "memory/sh4_tmu.h"
#include "platform/clock.h"
#include "platform/event_queue.h"
#include "pvr/holly_intc.h"
#include "pvr/pvr_regs.h"
#include "time_adapter.h"

#include <string>
#include <vector>

using namespace bs;
namespace nt = bs::native::time;
namespace ta = bs::transitional::time;

namespace {

// Sequencia de entregas "tipo@devido" de uma fila.
struct Recorder {
    std::vector<std::string> seen;
    void attach(nt::EventQueue& q) {
        q.set_observer([this](const nt::Event& e, nt::Instant) {
            seen.push_back(std::string(nt::event_kind_name(e.kind)) + "@" + std::to_string(e.due.ticks));
        });
    }
};

} // namespace

BS_TEST(n2_clock_is_monotonic_and_timebase_has_no_drift) {
    nt::LogicalClock clk({}, 1000);
    clk.advance_to({5});
    BS_CHECK(clk.now().ticks == 5);
    bool threw = false;
    try {
        clk.advance_to({4});  // o tempo logico nunca volta
    } catch (const nt::ClockError&) {
        threw = true;
    }
    BS_CHECK(threw && clk.now().ticks == 5);
    constexpr nt::TimeBase tb;  // campo NTSC exato
    static_assert(tb.ns(tb.frame_start(60000)) == 1001000000000ull);
    // 10^6 campos: 1 000 000 * 1001/60000 s = 16 683,333... s, sem deriva
    BS_CHECK(tb.ns(tb.frame_start(1000000)) == 16683333333333ull);
    BS_CHECK(tb.frame(tb.frame_start(1000000)) == 1000000);
    // base de compatibilidade do N2a: um tick por ciclo estimado
    constexpr nt::TimeBase compat{kCompatTicksPerSecond, kCompatTicksPerField};
    BS_CHECK(compat.frame(nt::Instant{kCompatTicksPerField * 3 - 1}) == 2);
    BS_CHECK(compat.seconds(nt::Instant{kCompatTicksPerSecond * 7 + 1}) == 7);
    bool bad = false;
    try {
        nt::LogicalClock z({0, 1});
    } catch (const nt::ClockError&) {
        bad = true;
    }
    BS_CHECK(bad);
}

BS_TEST(n2_clock_wall_time_follows_logical_time) {
    nt::LogicalClock clk({100, 10}, 1700000000);  // 100 ticks por segundo
    clk.advance_to({250});
    BS_CHECK(clk.wall_clock_unix() == 1700000002);
    clk.set_wall_clock(42);  // acerta no instante atual
    clk.advance_to({349});
    BS_CHECK(clk.wall_clock_unix() == 42);
    clk.advance_to({350});
    BS_CHECK(clk.wall_clock_unix() == 43);
}

// Ordem total: instante, prioridade do tipo, ordem de agendamento.
BS_TEST(n2_queue_orders_simultaneous_events_by_kind_then_sequence) {
    nt::EventQueue q;
    Recorder       r;
    r.attach(q);
    q.post(nt::EventKind::RenderDone, {10});
    q.post(nt::EventKind::Timer, {10});
    q.post(nt::EventKind::VBlankIn, {10});
    q.post(nt::EventKind::VBlankOut, {5});
    q.post(nt::EventKind::VBlankIn, {10});  // mesmo tipo e instante: depois do primeiro
    BS_CHECK(q.next_due().value().ticks == 5);
    BS_CHECK(q.run_until({9}) == 1);
    BS_CHECK(q.run_until({10}) == 4);
    const std::vector<std::string> want = {"VBlankOut@5", "VBlankIn@10", "VBlankIn@10", "RenderDone@10", "Timer@10"};
    BS_CHECK(r.seen == want);
    BS_CHECK(!q.next_due());
    // caso negativo (mutacao real do comparador): com a prioridade do VBlank-In invertida, a
    // mesma sequencia de agendamentos sai em outra ordem e a comparacao falha
    nt::EventQueue m;
    Recorder       rm;
    rm.attach(m);
    m.post(nt::EventKind::RenderDone, {10});
    m.post(nt::EventKind::Timer, {10});
    m.post(nt::EventKind::VBlankIn, {10});
    m.post(nt::EventKind::VBlankOut, {5});
    m.post(nt::EventKind::VBlankIn, {10});
    m.set_kind_priority(nt::EventKind::VBlankIn, 200);  // depois de agendar: reordena os pendentes
    m.run_until({10});
    BS_CHECK(rm.seen != want);
    BS_CHECK((rm.seen == std::vector<std::string>{"VBlankOut@5", "RenderDone@10", "Timer@10", "VBlankIn@10", "VBlankIn@10"}));
}

BS_TEST(n2_queue_cancel_and_reschedule) {
    nt::EventQueue q;
    Recorder       r;
    r.attach(q);
    const nt::EventId a = q.post(nt::EventKind::RenderDone, {10});
    const nt::EventId b = q.post(nt::EventKind::InputDone, {20});
    const nt::EventId c = q.post(nt::EventKind::VBlankIn, {30});
    BS_CHECK(q.cancel(b) && !q.pending(b) && !q.cancel(b));
    BS_CHECK(q.reschedule(a, {40}) && q.pending(a));
    BS_CHECK(q.run_until({35}) == 1);  // so o VBlank (o render foi para 40; o Maple, cancelado)
    BS_CHECK(r.seen == std::vector<std::string>{"VBlankIn@30"});
    BS_CHECK(!q.reschedule(c, {50}));  // ja entregue
    BS_CHECK(q.run_until({40}) == 1 && r.seen.back() == "RenderDone@40");
    BS_CHECK(q.size() == 0);
}

// Assinantes, temporizador de cliente e agendamento durante a entrega.
BS_TEST(n2_queue_delivers_events_posted_during_delivery) {
    nt::EventQueue q;
    std::vector<uint64_t> vb;
    q.subscribe(nt::EventKind::VBlankIn, [&](const nt::Event& e, nt::Instant at) {
        vb.push_back(e.due.ticks);
        BS_CHECK(at.ticks == 100);  // entregue na passagem ate 100
        q.post(nt::EventKind::VBlankIn, {e.due.ticks + 30});  // proximo campo
    });
    q.post(nt::EventKind::VBlankIn, {10});
    uint64_t timerAt = 0;
    q.schedule_timer({55}, [&](nt::Instant at) { timerAt = at.ticks; });
    BS_CHECK(q.run_until({100}) == 5);  // VBlank 10, 40, 70, 100 e o temporizador
    BS_CHECK((vb == std::vector<uint64_t>{10, 40, 70, 100}));
    BS_CHECK(timerAt == 100);
    BS_CHECK(q.next_due().value().ticks == 130);
}

namespace {

constexpr uint64_t kField = kCompatTicksPerField;

// Mesmo roteiro de acessos ao hardware com o provedor atual: SPG de 263 linhas com VBlank-In e
// Out, leitura de ISTNRM (que sincroniza os eventos), reconhecimento, troca da linha de VBlank
// no meio, TMU livre e RTC. Devolve o que o jogo veria.
std::vector<uint32_t> hardwareScript() {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    pvr_regs_reset();
    holly_intc_reset(0);
    sh4_tmu_reset();
    aica_rtc_set(0x906D38C5, 0);
    sh4_write32(&c, 0xA05F80D8, (262u << 16) | 857);  // SPG_LOAD: 263 linhas
    sh4_write32(&c, 0xA05F80CC, (21u << 16) | 240);   // VBlank-In 240, Out 21
    sh4_write16(&c, 0xFFD80010, 0x0002);               // TCR0: P/64
    sh4_write8(&c, 0xFFD80004, 1);                     // TSTR: canal 0
    std::vector<uint32_t> seen;
    for (int step = 0; step < 400; ++step) {
        c.cycles += kField / 7 + uint64_t(step % 7) * 1013;  // passos irregulares (~57 campos)
        if (step == 150) sh4_write32(&c, 0xA05F80CC, (30u << 16) | 200);  // nova temporizacao
        const uint32_t ist = sh4_read32(&c, 0xA05F6900);
        seen.push_back(ist & (kIstVblankIn | kIstVblankOut));
        sh4_write32(&c, 0xA05F6900, ist & (kIstVblankIn | kIstVblankOut));  // reconhece
        seen.push_back(sh4_read32(&c, 0xFFD8000C));                         // TCNT0
        seen.push_back(sh4_read32(&c, 0xA05F810C));                         // SPG_STATUS
        seen.push_back(sh4_read32(&c, 0xA0710004));                         // RTC (16 bits baixos)
    }
    seen.push_back(uint32_t(holly_vblank_count()));
    return seen;
}

} // namespace

// A1 em escala de unidade: o provedor nativo da o mesmo que o legado ao jogo.
BS_TEST(n2_native_time_provider_matches_legacy) {
    ta::uninstall();
    const std::vector<uint32_t> legacy = hardwareScript();
    ta::install({});
    const std::vector<uint32_t> native = hardwareScript();
    const ta::Stats             st     = ta::stats();
    ta::uninstall();
    BS_CHECK(legacy.back() > 50);  // houve VBlanks
    BS_CHECK(native == legacy);
    BS_CHECK(st.clockRegressions == 0 && st.delivered[0] == legacy.back());
}

// Caso negativo: perder eventos tem de aparecer na comparacao.
BS_TEST(n2_lost_event_mutation_is_detected) {
    ta::uninstall();
    const std::vector<uint32_t> legacy = hardwareScript();
    ta::install({"", "perder_vblank_out"});
    const std::vector<uint32_t> mutated = hardwareScript();
    ta::uninstall();
    BS_CHECK(mutated != legacy);
    bool threw = false;
    try {
        ta::install({"", "mutacao_que_nao_existe"});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    BS_CHECK(threw && !ta::installed());
}

namespace {

// Fim de render, Maple e blocos de disco agendados, reagendados e cancelados.
std::vector<uint32_t> scheduledScript() {
    TimeProvider& p = time_provider();
    pvr_regs_reset();
    holly_intc_reset(0);
    std::vector<uint32_t> out;
    p.render_scheduled(5000);
    p.maple_scheduled(7000);
    p.disc_scheduled(6500);
    for (uint64_t t = 1000; t <= 12000; t += 1000) {
        if (t == 4000) p.render_scheduled(9000);  // novo STARTRENDER antes do fim
        if (t == 6000) p.maple_cancel();          // visto antes pelo controlador
        if (t == 8000) p.maple_scheduled(8500);
        out.push_back(p.holly_due(t) & (kDueRender | kDueMaple));
        out.push_back(p.disc_due(t) ? 1u : 0u);
        if (t == 7000) p.disc_scheduled(11000);
    }
    return out;
}

// Roteiro sob uma mutacao do provedor nativo (BS_TEMPO_MUTACAO).
template <class F>
auto underMutation(const char* mutation, F script) {
    ta::uninstall();
    ta::install({"", mutation});
    auto out = script();
    ta::uninstall();
    return out;
}

} // namespace

// Fim de render, Maple e bloco de disco: mesmos vencimentos nos dois provedores.
BS_TEST(n2_scheduled_events_match_legacy) {
    ta::uninstall();
    const auto legacy = scheduledScript();
    ta::install({});
    const auto native = scheduledScript();
    ta::uninstall();
    BS_CHECK(native == legacy);
    // render so em 9000, Maple so em 8500 (o de 7000 foi cancelado), disco em 6500 e 11000
    BS_CHECK(legacy[2 * 8] == kDueRender + kDueMaple && legacy[2 * 6 + 1] == 1 && legacy[2 * 10 + 1] == 1);
}

// Casos negativos com defeitos reais no provedor nativo: evento cancelado entregue, deriva do
// VBlank e relogio consultavel parado tem de mudar o que o jogo veria.
BS_TEST(n2_mutations_are_detected) {
    ta::uninstall();
    const auto legacySched = scheduledScript();
    const auto legacyHw    = hardwareScript();
    BS_CHECK(underMutation("ignorar_cancelamento", scheduledScript) != legacySched);
    BS_CHECK(underMutation("deriva", hardwareScript) != legacyHw);
    BS_CHECK(underMutation("relogio_degraus", hardwareScript) != legacyHw);
    BS_CHECK(underMutation("atrasar_render", scheduledScript) != legacySched);
}

// Revisao Codex 2026-10-15: o reset do hardware (holly_intc_reset) nao muda a hora do RTC no
// legado; o nativo recriava o relogio e zerava a base. Inclui a volta dos 32 bits.
BS_TEST(n2_rtc_survives_reset_like_legacy) {
    auto run = [] {
        TimeProvider&         p = time_provider();
        std::vector<uint32_t> out;
        p.reset(0);
        p.rtc_set(0xFFFFFFF0u, 1000);
        out.push_back(p.rtc_seconds(1000 + 5 * kCompatTicksPerSecond));
        p.reset(2 * kCompatTicksPerSecond);  // sessao do hardware reiniciada
        out.push_back(p.rtc_seconds(30 * kCompatTicksPerSecond));  // passou de 0xFFFFFFFF
        p.reset(0);                                                 // reset para tras
        out.push_back(p.rtc_seconds(0));                            // antes da referencia
        out.push_back(p.rtc_seconds(500));
        return out;
    };
    ta::uninstall();
    const auto legacy = run();
    ta::install({});
    const auto native = run();
    ta::uninstall();
    BS_CHECK(legacy[0] == 0xFFFFFFF5u && legacy[1] == 0x0000000Du && legacy[2] == 0xFFFFFFF0u);
    BS_CHECK(native == legacy);
}

// Revisao Codex 2026-10-15: INIT_SYSTEM do GD-ROM descarta a transferencia; o bloco agendado
// na fila nativa nao pode ser entregue depois.
BS_TEST(n2_gdrom_init_system_cancels_pending_block) {
    auto run = [] {
        std::vector<uint8_t> ram(16u << 20);
        Sh4Context           c{};
        c.ram = ram.data();
        TimeProvider& p = time_provider();
        pvr_regs_reset();
        holly_intc_reset(0);
        p.disc_scheduled(5000);
        gdrom_syscall(&c, 3, 0, 0);  // INIT_SYSTEM
        return p.disc_due(10000);
    };
    ta::uninstall();
    BS_CHECK(!run());
    ta::install({});
    BS_CHECK(!run());
    ta::uninstall();
}

// Revisao Codex 2026-10-15: cancelar uma inscricao vale na hora, inclusive durante a entrega;
// uma inscricao nova so ve os proximos eventos.
BS_TEST(n2_queue_unsubscribe_takes_effect_during_delivery) {
    nt::EventQueue           q;
    std::vector<std::string> calls;
    nt::SubscriptionId       b, self, late;
    q.subscribe(nt::EventKind::VBlankIn, [&](const nt::Event&, nt::Instant) {
        calls.push_back("a");
        q.unsubscribe(b);  // b vem depois de a: nao pode ser chamado
        if (!late) late = q.subscribe(nt::EventKind::VBlankIn, [&](const nt::Event&, nt::Instant) { calls.push_back("late"); });
    });
    b    = q.subscribe(nt::EventKind::VBlankIn, [&](const nt::Event&, nt::Instant) { calls.push_back("b"); });
    self = q.subscribe(nt::EventKind::VBlankIn, [&](const nt::Event&, nt::Instant) {
        calls.push_back("self");
        q.unsubscribe(self);
    });
    q.post(nt::EventKind::VBlankIn, {1});
    q.post(nt::EventKind::VBlankIn, {2});
    q.run_until({2});
    BS_CHECK((calls == std::vector<std::string>{"a", "self", "a", "late"}));
}
