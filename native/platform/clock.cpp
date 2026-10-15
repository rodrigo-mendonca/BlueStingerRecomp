#include "platform/clock.h"

#include <chrono>
#include <ctime>

namespace bs::native::time {

LogicalClock::LogicalClock(TimeBase base, int64_t wall_at_start) : base_(base), wallBase_(wall_at_start) {
    if (base_.ticks_per_second == 0 || base_.ticks_per_frame == 0)
        throw ClockError("base de tempo invalida: ticks por segundo e por quadro precisam ser positivos");
}

uint64_t LogicalClock::host_ns() const {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

int64_t LogicalClock::wall_clock_unix() const {
    const uint64_t elapsed = now_.ticks >= wallRef_.ticks ? now_.ticks - wallRef_.ticks : 0;
    return wallBase_ + int64_t(elapsed / base_.ticks_per_second);
}

void LogicalClock::advance_to(Instant t) {
    if (t < now_) throw ClockError("relogio logico nao pode voltar no tempo");
    now_ = t;
}

void LogicalClock::set_wall_clock(int64_t seconds) { set_wall_clock(seconds, now_); }

void LogicalClock::set_wall_clock(int64_t seconds, Instant at) {
    wallBase_ = seconds;
    wallRef_  = at;
}

int64_t LogicalClock::host_wall_clock_unix() {
    // hora local de parede: o deslocamento do fuso entra nos segundos (o jogo mostra a hora local)
    const std::time_t t = std::time(nullptr);
    std::tm           local{};
#ifdef _WIN32
    localtime_s(&local, &t);
    return int64_t(_mkgmtime(&local));
#else
    localtime_r(&t, &local);
    return int64_t(timegm(&local));
#endif
}

} // namespace bs::native::time
