#include "platform/progress.h"

namespace bs::native::time {

ProgressMonitor::ProgressMonitor(const IClock& clock, const IEventQueue& queue, uint64_t stall_units)
    : clock_(clock), queue_(queue), stallUnits_(stall_units) {}

void ProgressMonitor::work(ContextId ctx, uint64_t units) {
    if (!units) return;
    ctx_[ctx.value].total += units;
    windowBy_[ctx.value] += units;
    window_ += units;
    if (window_ > maxWindow_) maxWindow_ = window_;
    if (stallUnits_ && !stalled_ && window_ > stallUnits_) {
        stalled_ = true;
        ++stalls_;
        uint32_t blamed = ctx.value;
        uint64_t most   = 0;
        for (const auto& [id, w] : windowBy_)
            if (w > most) most = w, blamed = id;
        ++ctx_[blamed].stalls_blamed;
        lastBlamed_ = ContextId{blamed};
        if (observer_) observer_({RecordKind::Stall, ContextId{blamed}, {}, clock_.now(), {}, window_});
    }
}

ProgressReport ProgressMonitor::at_wait(ContextId ctx, WaitId wait) {
    ContextStats& c = ctx_[ctx.value];
    WaitStats&    w = waits_[wait.value];
    if (c.in_wait && c.wait != wait) endEpisode(c, ctx, false);  // outro ponto: o anterior acabou sem saida vista
    if (!c.in_wait) {
        c.in_wait        = true;
        c.wait           = wait;
        c.iterations     = 0;
        c.delivered_mask = 0;
        c.last_kind      = -1;
        c.advances       = 0;
        c.last_target    = {};
        c.begin          = clock_.now();
        ++w.episodes;
        if (observer_) observer_({RecordKind::EpisodeBegin, ctx, wait, clock_.now()});
    }
    ++c.iterations;
    ++c.waits;
    ++w.iterations;
    if (observer_ && windowReport_ && window_ >= windowReport_) {
        uint32_t most = ctx.value;  // quem mais trabalhou na janela que fecha
        uint64_t top  = 0;
        for (const auto& [id, u] : windowBy_)
            if (u > top) top = u, most = id;
        observer_({RecordKind::Window, ContextId{most}, wait, clock_.now(), {}, window_});
    }
    window_  = 0;  // o tempo poderia andar aqui: a janela de trabalho fecha
    stalled_ = false;
    windowBy_.clear();

    ProgressReport r;
    const auto next = queue_.next_due();
    if (!next) {
        r.decision = ProgressDecision::Deadlock;
        ++w.deadlocks;
        if (observer_) observer_({RecordKind::Deadlock, ctx, wait, clock_.now()});
    } else if (*next <= clock_.now()) {
        r.decision = ProgressDecision::Continue;  // evento vencido: entregar antes de andar o tempo
    } else {
        r.decision   = ProgressDecision::AdvanceTime;
        r.advance_to = *next;
        if (c.advances == 0 || c.last_target != *next) {
            ++c.advances;
            ++w.advances;
            c.last_target = *next;
            if (observer_) observer_({RecordKind::Advance, ctx, wait, clock_.now(), *next});
        }
    }
    return r;
}

void ProgressMonitor::repeat_wait(ContextId ctx, WaitId wait, uint64_t n) {
    ContextStats& c = ctx_[ctx.value];
    if (!n || !c.in_wait || c.wait != wait) return;
    c.iterations += n;
    c.waits += n;
    c.total += n;  // o poll de cada volta (a janela fecha a cada volta: nao muda)
    waits_[wait.value].iterations += n;
}

void ProgressMonitor::wait_done(ContextId ctx, WaitId wait) {
    ContextStats& c = ctx_[ctx.value];
    if (c.in_wait && c.wait == wait) endEpisode(c, ctx, true);
}

void ProgressMonitor::delivered(EventKind kind, Instant) {
    for (auto& [id, c] : ctx_)
        if (c.in_wait) {
            c.delivered_mask |= 1u << static_cast<unsigned>(kind);
            c.last_kind = static_cast<int>(kind);
        }
}

void ProgressMonitor::endEpisode(ContextStats& c, ContextId ctx, bool exited) {
    WaitStats& w = waits_[c.wait.value];
    if (exited) {
        if (c.delivered_mask) {
            ++w.exits_with_event;
            ++w.released_by[size_t(c.last_kind)];
        } else ++w.exits_without_event;
        if (w.expected >= 0) {
            if (c.delivered_mask & (1u << unsigned(w.expected))) ++w.exits_with_expected;
            else ++w.exits_without_expected;
        }
        const uint64_t d = clock_.now().ticks - c.begin.ticks;
        if (d > w.max_ticks) w.max_ticks = d;
    }
    if (observer_) {
        Record r{RecordKind::EpisodeEnd, ctx, c.wait, clock_.now(), c.begin, c.iterations, c.delivered_mask};
        r.released_by = exited ? c.last_kind : -2;  // -2: episodio interrompido (outro ponto de espera)
        observer_(r);
    }
    c.in_wait = false;
}

} // namespace bs::native::time
