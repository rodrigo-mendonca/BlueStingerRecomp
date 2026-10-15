#pragma once
// N2a: relogio logico nativo (IClock). O instante so anda por advance_to, chamado por quem
// dirige o tempo (no hibrido, o adaptador de transicao); nunca volta. A data/hora de parede e
// a hora do host no inicio da sessao mais o tempo logico decorrido, ou um valor fixo nos
// testes (D5).
#include "platform/time.h"

#include <cstdint>
#include <stdexcept>

namespace bs::native::time {

class ClockError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class LogicalClock final : public IClock {
public:
    // `wall_at_start`: data/hora de parede (segundos, epoca Unix) no instante 0.
    explicit LogicalClock(TimeBase base = {}, int64_t wall_at_start = host_wall_clock_unix());

    Instant  now() const override { return now_; }
    TimeBase time_base() const override { return base_; }
    uint64_t host_ns() const override;
    int64_t  wall_clock_unix() const override;

    // Avanca o relogio; instante anterior ao atual e erro (o tempo logico nunca volta).
    void advance_to(Instant t);
    // Acerta a data/hora de parede para `seconds` no instante atual.
    void set_wall_clock(int64_t seconds);
    // Acerta a data/hora de parede: valia `seconds` no instante `at` (antes de `at`, vale `seconds`).
    void set_wall_clock(int64_t seconds, Instant at);

    // Hora local de parede do host agora, em segundos desde a epoca Unix.
    static int64_t host_wall_clock_unix();

private:
    TimeBase base_;
    Instant  now_;
    int64_t  wallBase_ = 0;   // segundos de parede em wallRef_
    Instant  wallRef_;
};

} // namespace bs::native::time
