#include "time_adapter.h"

#include "cpu/time_hooks.h"
#include "platform/clock.h"
#include "platform/event_queue.h"
#include "pvr/pvr_regs.h"

#include <cstdio>
#include <memory>
#include <stdexcept>

namespace bs::transitional::time {
namespace nt = bs::native::time;

namespace {

// 1950-01-01 (epoca do RTC do Dreamcast) a 1970-01-01: 20 anos com 5 bissextos
constexpr int64_t kDcEpochDelta = 7305ll * 86400;
constexpr uint64_t kMutationDelay = 2000;  // "atrasar_render": um quantum de poll
constexpr uint64_t kMutationDrift = 1000;  // "deriva": ticks somados a cada VBlank-In (~1 campo em 3 300)
constexpr uint64_t kMutationStep  = kCompatTicksPerField / 8;  // "relogio_degraus"

class NativeTime final : public TimeProvider {
public:
    explicit NativeTime(const Options& opt) {
        if (opt.mutation == "perder_vblank_out") dropVblankOut_ = true;
        else if (opt.mutation == "atrasar_render") delayRender_ = true;
        else if (opt.mutation == "ignorar_cancelamento") ignoreCancel_ = true;
        else if (opt.mutation == "deriva") drift_ = true;
        else if (opt.mutation == "relogio_degraus") coarseClock_ = true;
        else if (!opt.mutation.empty()) throw std::invalid_argument("BS_TEMPO_MUTACAO desconhecida: " + opt.mutation);
        if (!opt.trace.empty()) {
            trace_ = std::fopen(opt.trace.c_str(), "w");
            if (!trace_) throw std::invalid_argument("nao foi possivel criar o trace de tempo: " + opt.trace);
            std::fprintf(trace_, "# fila de eventos nativa (N2a): seq, tipo, instante devido, instante de entrega (ticks),\n"
                                 "# linha: 1 se o devido de um VBlank cai exatamente na linha do SPG (- nos outros tipos)\n");
            std::fprintf(trace_, "seq\ttipo\tdevido\tentrega\tlinha\n");
        }
        start(0);
    }
    ~NativeTime() override {
        if (trace_) std::fclose(trace_);
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
        if (due) render_ = post(display_, nt::EventKind::RenderDone, due + (delayRender_ ? kMutationDelay : 0));
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

    Stats stats;

private:
    void start(uint64_t now) {
        clock_ = std::make_unique<nt::LogicalClock>(nt::TimeBase{kCompatTicksPerSecond, kCompatTicksPerField}, 0);
        clock_->advance_to(nt::Instant{now});
        if (rtcSet_) clock_->set_wall_clock(rtcSeconds_, nt::Instant{rtcRef_});
        display_ = nt::EventQueue{};
        disc_    = nt::EventQueue{};
        vbIn_ = vbOut_ = render_ = maple_ = discEv_ = {};
        last_ = now;
        for (auto* q : {&display_, &disc_})
            q->set_observer([this](const nt::Event& e, nt::Instant at) { observe(e, at); });
        display_.subscribe(nt::EventKind::VBlankIn, [this](const nt::Event& e, nt::Instant) {
            due_ |= kDueVblankIn;
            vbIn_ = postCrossing(nt::EventKind::VBlankIn, e.due.ticks);
        });
        display_.subscribe(nt::EventKind::VBlankOut, [this](const nt::Event& e, nt::Instant) {
            if (!dropVblankOut_) due_ |= kDueVblankOut;
            vbOut_ = postCrossing(nt::EventKind::VBlankOut, e.due.ticks);
        });
        display_.subscribe(nt::EventKind::RenderDone, [this](const nt::Event&, nt::Instant) {
            due_ |= kDueRender;
            render_ = {};
        });
        display_.subscribe(nt::EventKind::InputDone, [this](const nt::Event&, nt::Instant) {
            due_ |= kDueMaple;
            maple_ = {};
        });
        disc_.subscribe(nt::EventKind::FileDone, [this](const nt::Event&, nt::Instant) {
            discFired_ = true;
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

    nt::EventId post(nt::EventQueue& q, nt::EventKind k, uint64_t due) {
        ++stats.posted;
        return q.post(k, nt::Instant{due});
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

    void observe(const nt::Event& e, nt::Instant at) {
        ++stats.delivered[static_cast<size_t>(e.kind)];
        if (!trace_) return;
        // criterio A2: o devido de um VBlank tem de ser o cruzamento da linha do SPG, conferido
        // com a conta do legado (pvr_spg_crossed), independente de pvr_spg_next_crossing
        const char* line = "-";
        if (e.kind == nt::EventKind::VBlankIn || e.kind == nt::EventKind::VBlankOut) {
            const uint32_t l = e.kind == nt::EventKind::VBlankIn ? pvr_vblank_in_line() : pvr_vblank_out_line();
            line = e.due.ticks > 0 && pvr_spg_crossed(e.due.ticks - 1, e.due.ticks, l) ? "1" : "0";
        }
        std::fprintf(trace_, "%llu\t%s\t%llu\t%llu\t%s\n", static_cast<unsigned long long>(traceSeq_++),
                     nt::event_kind_name(e.kind), static_cast<unsigned long long>(e.due.ticks),
                     static_cast<unsigned long long>(at.ticks), line);
    }

    std::unique_ptr<nt::LogicalClock> clock_;
    nt::EventQueue                    display_, disc_;
    nt::EventId                       vbIn_, vbOut_, render_, maple_, discEv_;
    uint64_t                          last_      = 0;
    uint32_t                          due_       = 0;
    bool                              discFired_ = false;
    bool                              dropVblankOut_ = false, delayRender_ = false, ignoreCancel_ = false;
    bool                              drift_ = false, coarseClock_ = false;
    uint64_t                          driftTotal_ = 0;
    bool                              rtcSet_     = false;
    int64_t                           rtcSeconds_ = 0;  // hora de parede (Unix) em rtcRef_
    uint64_t                          rtcRef_     = 0;
    std::FILE*                        trace_    = nullptr;
    uint64_t                          traceSeq_ = 0;
};

std::unique_ptr<NativeTime> g_native;
Stats                       g_lastStats;

} // namespace

void install(const Options& opt) {
    uninstall();
    g_native = std::make_unique<NativeTime>(opt);
    time_set_provider(g_native.get());
}

void uninstall() {
    if (!g_native) return;
    time_set_provider(nullptr);
    g_lastStats = g_native->stats;
    g_native.reset();
}

bool installed() { return g_native != nullptr; }

Stats stats() { return g_native ? g_native->stats : g_lastStats; }

} // namespace bs::transitional::time
