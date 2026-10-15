#pragma once
// N2a: fila de eventos nativa (IEventQueue). Ordem total e deterministica: instante devido,
// prioridade do tipo (ordem de EventKind) e ordem de agendamento. Sem threads: quem dirige o
// tempo chama run_until.
#include "platform/time.h"

#include <array>
#include <cstdint>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace bs::native::time {

class EventQueue final : public IEventQueue {
public:
    EventId                post(EventKind kind, Instant due, uint64_t payload = 0) override;
    bool                   cancel(EventId id) override;
    bool                   reschedule(EventId id, Instant due) override;
    bool                   pending(EventId id) const override;
    std::optional<Instant> next_due() const override;
    SubscriptionId         subscribe(EventKind kind, Handler h) override;
    void                   unsubscribe(SubscriptionId id) override;
    EventId                schedule_timer(Instant at, std::function<void(Instant)> cb) override;
    size_t                 run_until(Instant until) override;
    void                   set_kind_priority(EventKind kind, uint8_t priority) override;

    // Observador de todas as entregas (diagnostico: trace da fila), antes dos assinantes.
    void   set_observer(Handler h) { observer_ = std::move(h); }
    size_t size() const { return order_.size(); }
    void   clear();

private:
    using Key = std::tuple<uint64_t, uint8_t, uint64_t>;  // (ticks, prioridade, sequencia)
    struct Pending {
        Event                         ev;
        Key                           key;
        std::function<void(Instant)>  timer;  // so temporizadores de cliente
    };
    Key keyOf(EventKind kind, Instant due) { return {due.ticks, priority_[static_cast<size_t>(kind)], seq_++}; }
    std::array<uint8_t, static_cast<size_t>(EventKind::Count)> priority_ = [] {
        std::array<uint8_t, static_cast<size_t>(EventKind::Count)> p{};
        for (size_t i = 0; i < p.size(); ++i) p[i] = uint8_t(i);
        return p;
    }();

    std::map<Key, uint32_t>                 order_;    // chave -> id
    std::unordered_map<uint32_t, Pending>   events_;   // id -> evento pendente
    std::vector<std::pair<SubscriptionId, std::pair<EventKind, Handler>>> subs_;
    Handler  observer_;
    uint64_t seq_     = 0;
    uint32_t nextId_  = 1;
    uint32_t nextSub_ = 1;
};

} // namespace bs::native::time
