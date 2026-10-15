#include "platform/event_queue.h"

#include <algorithm>

namespace bs::native::time {

EventId EventQueue::post(EventKind kind, Instant due, uint64_t payload) {
    uint32_t id = nextId_++;
    if (id == 0) id = nextId_++;  // 0 e invalido
    Pending p;
    p.ev  = {EventId{id}, kind, due, payload};
    p.key = keyOf(kind, due);
    order_.emplace(p.key, id);
    events_.emplace(id, std::move(p));
    return EventId{id};
}

bool EventQueue::cancel(EventId id) {
    const auto it = events_.find(id.value);
    if (it == events_.end()) return false;
    order_.erase(it->second.key);
    events_.erase(it);
    return true;
}

bool EventQueue::reschedule(EventId id, Instant due) {
    const auto it = events_.find(id.value);
    if (it == events_.end()) return false;
    order_.erase(it->second.key);
    it->second.ev.due = due;
    it->second.key    = keyOf(it->second.ev.kind, due);
    order_.emplace(it->second.key, id.value);
    return true;
}

bool EventQueue::pending(EventId id) const { return events_.count(id.value) != 0; }

std::optional<Instant> EventQueue::next_due() const {
    if (order_.empty()) return std::nullopt;
    return Instant{std::get<0>(order_.begin()->first)};
}

SubscriptionId EventQueue::subscribe(EventKind kind, Handler h) {
    const SubscriptionId id{nextSub_++};
    subs_.push_back({id, {kind, std::move(h)}});
    return id;
}

void EventQueue::unsubscribe(SubscriptionId id) {
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(), [&](const auto& s) { return s.first == id; }), subs_.end());
}

EventId EventQueue::schedule_timer(Instant at, std::function<void(Instant)> cb) {
    const EventId id = post(EventKind::Timer, at);
    events_[id.value].timer = std::move(cb);
    return id;
}

size_t EventQueue::run_until(Instant until) {
    size_t n = 0;
    while (!order_.empty() && std::get<0>(order_.begin()->first) <= until.ticks) {
        const uint32_t id = order_.begin()->second;
        order_.erase(order_.begin());
        auto    node = events_.extract(id);
        Pending p    = std::move(node.mapped());
        ++n;
        if (observer_) observer_(p.ev, until);
        if (p.timer) p.timer(until);
        // copia: um assinante pode inscrever ou cancelar outros durante a entrega. Quem for
        // cancelado antes da sua vez nao e chamado; quem se inscreve agora so ve os proximos.
        const auto subs = subs_;
        for (const auto& [sid, s] : subs) {
            if (s.first != p.ev.kind) continue;
            const SubscriptionId id = sid;
            if (std::none_of(subs_.begin(), subs_.end(), [&](const auto& x) { return x.first == id; })) continue;
            s.second(p.ev, until);
        }
    }
    return n;
}

void EventQueue::set_kind_priority(EventKind kind, uint8_t priority) {
    priority_[static_cast<size_t>(kind)] = priority;
    // eventos pendentes passam a ser ordenados pela nova prioridade
    std::map<Key, uint32_t> reordered;
    for (auto& [id, p] : events_) {
        p.key = {std::get<0>(p.key), priority_[static_cast<size_t>(p.ev.kind)], std::get<2>(p.key)};
        reordered.emplace(p.key, id);
    }
    order_.swap(reordered);
}

void EventQueue::clear() {
    order_.clear();
    events_.clear();
}

} // namespace bs::native::time
