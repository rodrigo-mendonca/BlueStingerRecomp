#pragma once
// Tempo e eventos (N2a/N2b). Substitui ciclos estimados, TMU, RTC, VBlank do Holly e a
// entrega de interrupcoes: o jogo passa a ser dirigido por um relogio logico de quadros e
// por uma fila de eventos com ordem deterministica (docs/PLANO-PORT-NATIVO.md, secao 5).
#include "core/handle.h"

#include <cstdint>
#include <functional>

namespace bs::native::time {

// Instante logico: quadro do jogo e fracao (determinismo de roteiros) + tempo real do host.
struct Instant {
    uint64_t frame    = 0;  // campos logicos desde o inicio (NTSC: 60000/1001 Hz)
    uint32_t subframe = 0;  // 0..65535 dentro do quadro
};

// Taxa racional: o campo NTSC dura exatamente 1001/60000 s. Com 60 Hz inteiro, 60 000 campos
// dariam 1 000 s em vez de 1 001 s (revisao Codex 2026-10-15).
struct Rate {
    uint32_t num = 60000, den = 1001;  // campos por segundo = num / den
    // instante do campo `frame` em nanossegundos, sem acumular erro
    constexpr uint64_t frame_ns(uint64_t frame) const {
        return frame * den / num * 1000000000ull + (frame * den % num) * 1000000000ull / num;
    }
};

class IClock {
public:
    virtual ~IClock() = default;
    virtual Instant  now() const = 0;
    virtual uint64_t host_ns() const = 0;     // relogio monotono do host (medicao, nao logica)
    virtual Rate     frame_rate() const = 0;
    virtual int64_t  wall_clock_unix() const = 0;  // data/hora (substitui o RTC)
};

// Fontes de evento que hoje sao interrupcoes; a ordem entre eventos simultaneos e fixa.
enum class EventKind : uint8_t {
    VBlankIn,      // inicio do retraco vertical
    VBlankOut,
    RenderDone,    // fim de renderizacao de um quadro
    Timer,         // temporizador pedido pelo jogo
    FileDone,      // conclusao de leitura assincrona
    InputPoll,     // amostragem de controles
    AudioTick,
};

struct EventTag;
using EventId = Handle<EventTag>;

class IEventQueue {
public:
    virtual ~IEventQueue() = default;
    using Callback = std::function<void(EventKind, Instant)>;
    // Assinatura de um evento; a ordem de chamada entre assinaturas do mesmo tipo e a ordem de registro.
    virtual EventId subscribe(EventKind kind, Callback cb) = 0;
    virtual void    unsubscribe(EventId id) = 0;
    virtual EventId schedule_timer(Instant at, Callback cb) = 0;  // EventKind::Timer
    // Avanca o relogio logico ate `until`, entregando os eventos em ordem.
    virtual void run_until(Instant until) = 0;
};

} // namespace bs::native::time
