#include "time_adapter.h"

#include "cpu/time_hooks.h"
#include "platform/clock.h"
#include "platform/event_queue.h"
#include "platform/progress.h"
#include "pvr/holly_intc.h"
#include "pvr/pvr_regs.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace bs::transitional::time {
namespace nt = bs::native::time;

namespace {

// 1950-01-01 (epoca do RTC do Dreamcast) a 1970-01-01: 20 anos com 5 bissextos
constexpr int64_t kDcEpochDelta = 7305ll * 86400;
constexpr uint64_t kMutationDelay = 2000;  // "atrasar_render": um quantum de poll
constexpr uint64_t kMutationDrift = 1000;  // "deriva": ticks somados a cada VBlank-In (~1 campo em 3 300)
constexpr uint64_t kMutationStep  = kCompatTicksPerField / 8;  // "relogio_degraus"
constexpr uint64_t kMutationEvery = 97;    // mutacoes de VBlank: uma vez a cada 97 VBlank-In
constexpr uint64_t kMutationRender = 500;  // "render_perdido": o 500o fim de render
constexpr uint32_t kVblankWaitPc  = 0x8C064A2C;  // "sem_espera": espera do VBlank do jogo
constexpr uint32_t kRenderWaitPc  = 0x8C173086;  // espera do fim do render anterior (timeout 166 ms)
constexpr uint64_t kWindowReport  = 100000;      // BS_PROGRESS_TRACE: janelas a partir daqui (calibracao D8)

class NativeTime final : public TimeProvider {
public:
    explicit NativeTime(const Options& opt) : stallUnits_(opt.stall_units) {
        const std::string& m = opt.mutation;
        if (m == "perder_vblank_out") dropVblankOut_ = true;
        else if (m == "atrasar_render") delayRender_ = true;
        else if (m == "ignorar_cancelamento") ignoreCancel_ = true;
        else if (m == "deriva") drift_ = true;
        else if (m == "relogio_degraus") coarseClock_ = true;
        else if (m == "perder_vblank_in") dropVblankIn_ = true;
        else if (m == "vblank_extra") extraVblank_ = true;
        else if (m == "fundir_vblank") fuseVblank_ = true;
        else if (m == "render_perdido") dropRender_ = true;
        else if (m == "lento") slow_ = true;
        else if (m == "sem_espera") skipWait_ = true;
        else if (!m.empty()) throw std::invalid_argument("BS_TEMPO_MUTACAO desconhecida: " + m);
        if (!opt.trace.empty()) {
            trace_ = std::fopen(opt.trace.c_str(), "w");
            if (!trace_) throw std::invalid_argument("nao foi possivel criar o trace de tempo: " + opt.trace);
            std::fprintf(trace_, "# fila de eventos nativa (N2a): seq, tipo, instante devido, instante de entrega (ticks),\n"
                                 "# linha: 1 se o devido de um VBlank cai exatamente na linha do SPG (- nos outros tipos);\n"
                                 "# RenderStart (N2b-0): devido = fim de render agendado, entrega = instante do STARTRENDER\n");
            std::fprintf(trace_, "seq\ttipo\tdevido\tentrega\tlinha\n");
        }
        if (!opt.progress_trace.empty()) {
            ptrace_ = std::fopen(opt.progress_trace.c_str(), "w");
            if (!ptrace_) throw std::invalid_argument("nao foi possivel criar o trace de progresso: " + opt.progress_trace);
            std::fprintf(ptrace_, "# monitor de progresso (N2b-1, modo sombra): registro, VBlank, thread, ponto de espera, "
                                  "instante, alvo, valor (voltas ou trabalho), eventos no episodio (mascara), liberado por\n");
            std::fprintf(ptrace_, "reg\tvblank\tthread\tespera\tinstante\talvo\tvalor\teventos\tliberado\n");
        }
        start(0);
    }
    ~NativeTime() override {
        if (trace_) std::fclose(trace_);
        if (ptrace_) std::fclose(ptrace_);
    }

    const char* name() const override { return "nativo"; }

    void reset(uint64_t now) override { start(now); }

    uint64_t clock(uint64_t cycles) override {
        advance(cycles);
        // mutacao: relogio consultavel parado entre degraus de 1/8 de campo (continua andando, para
        // nao travar o poll central, que tambem le o instante por aqui)
        return coarseClock_ ? cycles - cycles % kMutationStep : cycles;
    }

    uint32_t holly_due(uint64_t now) override {
        advance(now);
        due_ = 0;
        display_.run_until(nt::Instant{now});
        last_ = now;
        return due_;
    }

    void spg_changed() override {
        display_.cancel(vbIn_);
        display_.cancel(vbOut_);
        vbIn_  = postCrossing(nt::EventKind::VBlankIn, last_);
        vbOut_ = postCrossing(nt::EventKind::VBlankOut, last_);
    }

    void render_scheduled(uint64_t due) override {
        cancel(display_, render_);
        if (!due) return;
        render_ = post(display_, nt::EventKind::RenderDone, due + (delayRender_ ? kMutationDelay : 0));
        if (trace_)  // N2b-0: medida da conclusao do render (inicio, fim agendado)
            std::fprintf(trace_, "%llu\tRenderStart\t%llu\t%llu\t-\n", static_cast<unsigned long long>(traceSeq_++),
                         static_cast<unsigned long long>(due), static_cast<unsigned long long>(clock_->now().ticks));
    }
    void maple_scheduled(uint64_t due) override {
        cancel(display_, maple_);
        maple_ = post(display_, nt::EventKind::InputDone, due);
    }
    void maple_cancel() override { cancel(display_, maple_); }

    void disc_scheduled(uint64_t due) override {
        cancel(disc_, discEv_);
        discEv_ = post(disc_, nt::EventKind::FileDone, due);
    }
    bool disc_due(uint64_t now) override {
        advance(now);
        discFired_ = false;
        disc_.run_until(nt::Instant{now});
        return discFired_;
    }
    void disc_cancel() override { cancel(disc_, discEv_); }

    uint32_t rtc_seconds(uint64_t now) override {
        advance(now);
        return uint32_t(clock_->wall_clock_unix() + kDcEpochDelta);
    }
    void rtc_set(uint32_t seconds, uint64_t now) override {
        advance(now);
        // guardado: um reset (sessao nova do hardware) recria o relogio mas nao muda a hora,
        // como no legado (revisao Codex 2026-10-15)
        rtcSet_     = true;
        rtcSeconds_ = int64_t(seconds) - kDcEpochDelta;
        rtcRef_     = now;
        clock_->set_wall_clock(rtcSeconds_, nt::Instant{rtcRef_});
    }

    // ---- N2b-1: modo sombra. So observa; nada aqui muda a execucao. ----
    void progress(uint32_t thread, uint64_t polls, uint32_t pc) override {
        flushRepeats();
        const uint64_t units = polls >= lastPolls_ ? polls - lastPolls_ : polls;  // contexto novo: recomeca
        lastPolls_           = polls;
        progress_->work(nt::ContextId{thread + 1}, units);
        if (thread < kMaxThreads && units) hot_[thread][pc] += units;  // lote desde o ultimo checkpoint, creditado ao PC dele
        if (progress_->stalls() != lastStalls_) {
            // D8 (modo sombra: aviso; no N2b-2, erro): a primeira falta, com o trabalho de cada
            // thread na janela e o ultimo PC dela (a que cruzou o limite nao e necessariamente a
            // culpada: no N2b-1 a thread 3 nunca passa por um ponto de espera)
            if (!lastStalls_) {
                std::string who;
                for (const auto& [id, w] : progress_->window_by()) {
                    const uint32_t t   = id - 1;
                    uint32_t       top = 0;
                    uint64_t       most = 0;
                    if (t < kMaxThreads)
                        for (const auto& [p, u] : hot_[t])
                            if (u > most) most = u, top = p;
                    char b[96];
                    std::snprintf(b, sizeof b, "%sthread %u %llu (maior lote em %08X)", who.empty() ? "" : ", ", t, ull(w), top);
                    who += b;
                }
                std::fprintf(stderr,
                             "[progresso] falta de progresso: %llu pontos de poll sem chegar a um ponto de espera "
                             "declarado (limite %llu); na janela: %s. Um laco de espera reescrito (mod) tem de "
                             "usar bs::mods::wait() em cada volta\n",
                             ull(progress_->window()), ull(stallUnits_), who.c_str());
            }
            lastStalls_ = progress_->stalls();
        }
    }
    void wait_point(uint32_t pc, uint32_t thread, uint64_t polls) override {
        if (skipWait_ && pc == kVblankWaitPc) {  // mutacao: espera nao declarada
            progress(thread, polls, pc);
            return;
        }
        // Volta repetida na mesma espera: so o poll da propria volta, sem evento entregue nem outro
        // trabalho no meio. Acumula (a decisao e a mesma) em vez de chamar o monitor a cada volta.
        if (repeatPc_ == pc && repeatThread_ == thread && polls - lastPolls_ <= 1 && polls >= lastPolls_) {
            lastPolls_ = polls;
            ++repeats_;
            return;
        }
        progress(thread, polls, pc);
        const nt::ProgressReport r = progress_->at_wait(nt::ContextId{thread + 1}, nt::WaitId{pc});
        for (auto& h : hot_)
            if (!h.empty()) h.clear();  // a janela fechou
        ++decisions_[static_cast<size_t>(r.decision)];
        repeatPc_       = pc;
        repeatThread_   = thread;
        repeatDecision_ = static_cast<size_t>(r.decision);
    }
    void wait_end(uint32_t pc, uint32_t thread, uint64_t polls) override {
        progress(thread, polls, pc);
        if (skipWait_ && pc == kVblankWaitPc) return;
        progress_->wait_done(nt::ContextId{thread + 1}, nt::WaitId{pc});
    }
    uint64_t last_vblank_due() const override { return lastVbDue_; }

    std::string progress_summary() {
        flushRepeats();
        std::string out;
        char        b[256];
        std::snprintf(b, sizeof b, "[progresso] sombra: maior janela %llu, faltas de progresso %llu (limite %llu); "
                                   "decisoes continuar %llu, avancar %llu, deadlock %llu\n",
                      ull(progress_->max_window()), ull(progress_->stalls()), ull(stallUnits_), ull(decisions_[0]),
                      ull(decisions_[1]), ull(decisions_[2]));
        out += b;
        for (const auto& [pc, w] : progress_->waits()) {
            std::snprintf(b, sizeof b, "[progresso] espera %08X: episodios %llu, voltas %llu, avancos %llu, saidas com "
                                       "evento %llu, sem evento %llu, deadlock %llu; liberada por",
                          pc, ull(w.episodes), ull(w.iterations), ull(w.advances), ull(w.exits_with_event),
                          ull(w.exits_without_event), ull(w.deadlocks));
            out += b;
            for (size_t k = 0; k < w.released_by.size(); ++k)
                if (w.released_by[k]) {
                    std::snprintf(b, sizeof b, " %s=%llu", nt::event_kind_name(nt::EventKind(k)), ull(w.released_by[k]));
                    out += b;
                }
            if (w.expected >= 0) {
                std::snprintf(b, sizeof b, "; evento esperado %s: com %llu, sem %llu",
                              nt::event_kind_name(nt::EventKind(w.expected)), ull(w.exits_with_expected),
                              ull(w.exits_without_expected));
                out += b;
            }
            std::snprintf(b, sizeof b, "; maior episodio %llu ticks\n", ull(w.max_ticks));
            out += b;
        }
        for (const auto& [id, c] : progress_->contexts()) {
            std::snprintf(b, sizeof b, "[progresso] thread %u: trabalho %llu, voltas em espera %llu, faltas atribuidas %llu\n",
                          id - 1, ull(c.total), ull(c.waits), ull(c.stalls_blamed));
            out += b;
        }
        return out;
    }

    Stats stats;

private:
    static unsigned long long ull(uint64_t v) { return static_cast<unsigned long long>(v); }

    void start(uint64_t now) {
        clock_ = std::make_unique<nt::LogicalClock>(nt::TimeBase{kCompatTicksPerSecond, kCompatTicksPerField}, 0);
        clock_->advance_to(nt::Instant{now});
        if (rtcSet_) clock_->set_wall_clock(rtcSeconds_, nt::Instant{rtcRef_});
        display_ = nt::EventQueue{};
        disc_    = nt::EventQueue{};
        vbIn_ = vbOut_ = render_ = maple_ = discEv_ = {};
        last_ = now;
        progress_  = std::make_unique<nt::ProgressMonitor>(*clock_, display_, stallUnits_);
        progress_->expect(nt::WaitId{kVblankWaitPc}, nt::EventKind::VBlankIn);
        progress_->expect(nt::WaitId{kRenderWaitPc}, nt::EventKind::RenderDone);
        frameTotals_.clear();
        lastPolls_ = 0;
        decisions_ = {};
        repeatPc_  = 0;
        repeats_   = 0;
        if (ptrace_) {
            progress_->set_observer([this](const nt::ProgressMonitor::Record& r) { traceProgress(r); });
            progress_->set_window_report(kWindowReport);
        }
        for (auto* q : {&display_, &disc_})
            q->set_observer([this](const nt::Event& e, nt::Instant at) { observe(e, at); });
        display_.subscribe(nt::EventKind::VBlankIn, [this](const nt::Event& e, nt::Instant) {
            if (e.payload == 1) {  // evento extra das mutacoes: so acende, nao reagenda
                applied(e, kDueVblankIn);
                return;
            }
            lastVbDue_ = e.due.ticks;
            ++vbCount_;
            if (ptrace_) traceFrame();
            const bool mutate = vbCount_ % kMutationEvery == 0;
            if (!(dropVblankIn_ && mutate)) applied(e, kDueVblankIn);
            if (slow_) {  // mutacao: trabalho extra do host a cada VBlank (criterio de desempenho)
                const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
                while (std::chrono::steady_clock::now() < until) {
                }
            }
            vbIn_ = postCrossing(nt::EventKind::VBlankIn, e.due.ticks);
            if (extraVblank_ && mutate)  // IRQ extra meio campo depois
                post(display_, nt::EventKind::VBlankIn, e.due.ticks + kCompatTicksPerField / 2, 1);
            if (fuseVblank_ && mutate) {  // o proximo VBlank cai junto com o seguinte: um so atendimento
                const auto ev = display_.pending(vbIn_) ? vbIn_ : nt::EventId{};
                if (ev) {
                    const uint32_t line = pvr_vblank_in_line();
                    const uint64_t next = pvr_spg_next_crossing(e.due.ticks, line);
                    const uint64_t next2 = pvr_spg_next_crossing(next, line);
                    display_.reschedule(ev, nt::Instant{next2});
                    post(display_, nt::EventKind::VBlankIn, next2, 1);
                }
            }
        });
        display_.subscribe(nt::EventKind::VBlankOut, [this](const nt::Event& e, nt::Instant) {
            if (!dropVblankOut_) applied(e, kDueVblankOut);
            vbOut_ = postCrossing(nt::EventKind::VBlankOut, e.due.ticks);
        });
        display_.subscribe(nt::EventKind::RenderDone, [this](const nt::Event& e, nt::Instant) {
            if (!(dropRender_ && ++renderCount_ == kMutationRender)) applied(e, kDueRender);
            render_ = {};
        });
        display_.subscribe(nt::EventKind::InputDone, [this](const nt::Event& e, nt::Instant) {
            applied(e, kDueMaple);
            maple_ = {};
        });
        disc_.subscribe(nt::EventKind::FileDone, [this](const nt::Event& e, nt::Instant at) {
            discFired_ = true;
            progress_->delivered(e.kind, at);
            discEv_    = {};
        });
        spg_changed();
    }

    // O instante de compatibilidade so anda para frente; um valor menor (sessao reiniciada sem
    // reset do provedor) e contado e nao move o relogio.
    void advance(uint64_t t) {
        if (t >= clock_->now().ticks) clock_->advance_to(nt::Instant{t});
        else ++stats.clockRegressions;
    }

    nt::EventId post(nt::EventQueue& q, nt::EventKind k, uint64_t due, uint64_t payload = 0) {
        ++stats.posted;
        return q.post(k, nt::Instant{due}, payload);
    }
    void cancel(nt::EventQueue& q, nt::EventId& id) {
        if (ignoreCancel_) return;  // mutacao: evento cancelado continua na fila
        if (id && q.cancel(id)) ++stats.cancelled;
        id = {};
    }
    // proximo cruzamento da linha de VBlank-In/Out depois de `after` (sem evento se a linha
    // esta fora do campo)
    nt::EventId postCrossing(nt::EventKind k, uint64_t after) {
        const uint32_t line = k == nt::EventKind::VBlankIn ? pvr_vblank_in_line() : pvr_vblank_out_line();
        const uint64_t at   = pvr_spg_next_crossing(after, line);
        if (at == kSpgNever) return {};
        if (drift_ && k == nt::EventKind::VBlankIn) driftTotal_ += kMutationDrift;  // mutacao
        return post(display_, k, at + (k == nt::EventKind::VBlankIn ? driftTotal_ : 0));
    }

    // O evento chegou de fato ao hardware (acende `bit` para o proximo holly_due) e so entao conta
    // para o monitor. Um VBlank-In com o bit ainda pendente no mesmo atendimento e uma fusao que o
    // Holly nao enxerga (ele so ve o bit): contado aqui (criterio B4).
    void applied(const nt::Event& e, uint32_t bit) {
        if (bit == kDueVblankIn && (due_ & kDueVblankIn)) ++stats.vblankCollapsed;
        due_ |= bit;
        progress_->delivered(e.kind, clock_->now());
    }

    // entrega as voltas acumuladas ao monitor (antes de qualquer outra observacao)
    void flushRepeats() {
        if (repeats_) {
            progress_->repeat_wait(nt::ContextId{repeatThread_ + 1}, nt::WaitId{repeatPc_}, repeats_);
            decisions_[repeatDecision_] += repeats_;
            repeats_ = 0;
        }
        repeatPc_ = 0;
    }

    void observe(const nt::Event& e, nt::Instant at) {
        flushRepeats();  // um evento muda a decisao: a proxima volta passa pelo monitor
        ++stats.delivered[static_cast<size_t>(e.kind)];
        if (!trace_) return;
        // criterio A2: o devido de um VBlank tem de ser o cruzamento da linha do SPG, conferido
        // com a conta do legado (pvr_spg_crossed), independente de pvr_spg_next_crossing
        const char* line = "-";
        if ((e.kind == nt::EventKind::VBlankIn || e.kind == nt::EventKind::VBlankOut) && e.payload == 0) {
            const uint32_t l = e.kind == nt::EventKind::VBlankIn ? pvr_vblank_in_line() : pvr_vblank_out_line();
            line = e.due.ticks > 0 && pvr_spg_crossed(e.due.ticks - 1, e.due.ticks, l) ? "1" : "0";
        }
        std::fprintf(trace_, "%llu\t%s\t%llu\t%llu\t%s\n", ull(traceSeq_++), nt::event_kind_name(e.kind),
                     ull(e.due.ticks), ull(at.ticks), line);
    }

    // Por VBlank: trabalho de cada thread no campo que terminou (referencia do criterio S4:
    // quanto o servidor do CRI, thread 3, roda por quadro). valor = trabalho; espera = thread.
    void traceFrame() {
        for (const auto& [id, c] : progress_->contexts()) {
            uint64_t& last = frameTotals_[id];
            if (c.total != last)
                std::fprintf(ptrace_, "quadro\t%llu\t%u\t-\t%llu\t0\t%llu\t0\t-1\n", ull(vbCount_), id - 1,
                             ull(clock_->now().ticks), ull(c.total - last));
            last = c.total;
        }
    }

    void traceProgress(const nt::ProgressMonitor::Record& r) {
        static const char* kNames[] = {"inicio", "avanco", "fim", "falta", "deadlock", "janela"};
        std::fprintf(ptrace_, "%s\t%llu\t%u\t%08X\t%llu\t%llu\t%llu\t%X\t%d\n", kNames[static_cast<int>(r.kind)],
                     ull(holly_vblank_count()), r.ctx.value - 1, r.wait.value, ull(r.now.ticks), ull(r.target.ticks),
                     ull(r.value), r.delivered_mask, r.released_by);
    }

    std::unique_ptr<nt::LogicalClock>    clock_;
    nt::EventQueue                       display_, disc_;
    nt::EventId                          vbIn_, vbOut_, render_, maple_, discEv_;
    std::unique_ptr<nt::ProgressMonitor> progress_;
    uint64_t                             stallUnits_ = 0, lastPolls_ = 0, lastStalls_ = 0;
    static constexpr uint32_t            kMaxThreads = 16;
    std::unordered_map<uint32_t, uint64_t> hot_[kMaxThreads];  // por thread: trabalho da janela por PC (mensagem da D8)
    std::array<uint64_t, 3>              decisions_{};
    std::map<uint32_t, uint64_t>         frameTotals_;
    uint32_t                             repeatPc_ = 0, repeatThread_ = 0;  // ultima espera vista
    size_t                               repeatDecision_ = 0;
    uint64_t                             repeats_ = 0;  // voltas acumuladas ainda nao entregues
    uint64_t                             last_      = 0, lastVbDue_ = 0, vbCount_ = 0, renderCount_ = 0;
    uint32_t                             due_       = 0;
    bool                                 discFired_ = false;
    bool dropVblankOut_ = false, delayRender_ = false, ignoreCancel_ = false, drift_ = false, coarseClock_ = false;
    bool dropVblankIn_ = false, extraVblank_ = false, fuseVblank_ = false, dropRender_ = false, slow_ = false;
    bool skipWait_ = false;
    uint64_t   driftTotal_ = 0;
    bool       rtcSet_     = false;
    int64_t    rtcSeconds_ = 0;  // hora de parede (Unix) em rtcRef_
    uint64_t   rtcRef_     = 0;
    std::FILE* trace_      = nullptr;
    std::FILE* ptrace_     = nullptr;
    uint64_t   traceSeq_   = 0;
};

std::unique_ptr<NativeTime> g_native;
Stats                       g_lastStats;
std::string                 g_lastProgress;

} // namespace

void install(const Options& opt) {
    uninstall();
    g_native = std::make_unique<NativeTime>(opt);
    time_set_provider(g_native.get());
}

void uninstall() {
    if (!g_native) return;
    time_set_provider(nullptr);
    g_lastStats    = g_native->stats;
    g_lastProgress = g_native->progress_summary();
    g_native.reset();
}

bool installed() { return g_native != nullptr; }

Stats stats() { return g_native ? g_native->stats : g_lastStats; }

std::string progress_summary() { return g_native ? g_native->progress_summary() : g_lastProgress; }

} // namespace bs::transitional::time
