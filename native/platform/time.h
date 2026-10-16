#pragma once
// Tempo e eventos (N2, docs/N2-PLANO.md). Relogio logico com base de tempo inteira, fila de
// eventos com ordem total e deterministica e (N2b) agendador cooperativo de tarefas. Nada aqui
// conhece o hardware de origem: quem cria o relogio escolhe a base de tempo (ticks por segundo
// e por quadro) e os produtores de evento.
#include "core/handle.h"

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>

namespace bs::native::time {

// Instante logico: ticks desde o inicio da sessao, na base de tempo do relogio.
struct Instant {
    uint64_t ticks = 0;
    constexpr auto operator<=>(const Instant&) const = default;
};

// Base de tempo: quantos ticks fazem um segundo e um quadro. Inteiros, para que a conversao
// nao acumule erro. O padrao e o campo NTSC exato (1001/60000 s): 60 000 000 ticks por
// segundo e 1 001 000 por quadro.
struct TimeBase {
    uint64_t ticks_per_second = 60000000;
    uint64_t ticks_per_frame  = 1001000;

    constexpr uint64_t frame(Instant t) const { return t.ticks / ticks_per_frame; }
    constexpr Instant  frame_start(uint64_t frame) const { return {frame * ticks_per_frame}; }
    // segundos inteiros e nanossegundos desde o instante 0, sem overflow nem deriva
    constexpr uint64_t seconds(Instant t) const { return t.ticks / ticks_per_second; }
    constexpr uint64_t ns(Instant t) const {
        return t.ticks / ticks_per_second * 1000000000ull + t.ticks % ticks_per_second * 1000000000ull / ticks_per_second;
    }
};

// Taxa racional de quadros (apresentacao e ritmo do host). O campo NTSC dura exatamente
// 1001/60000 s: com 60 Hz inteiro, 60 000 campos dariam 1 000 s em vez de 1 001 s.
struct Rate {
    uint32_t num = 60000, den = 1001;  // quadros por segundo = num / den
    constexpr uint64_t frame_ns(uint64_t frame) const {
        return frame * den / num * 1000000000ull + (frame * den % num) * 1000000000ull / num;
    }
};

class IClock {
public:
    virtual ~IClock() = default;
    virtual Instant  now() const = 0;
    virtual TimeBase time_base() const = 0;
    virtual uint64_t host_ns() const = 0;  // relogio monotono do host (medicao e ritmo, nao logica)
    // Data e hora em segundos (epoca Unix, hora local de parede). Jogo normal: hora real do
    // host no inicio da sessao + tempo logico decorrido. Testes: valor controlado (D5).
    virtual int64_t wall_clock_unix() const = 0;
};

// Tipos de evento. A ordem da enumeracao e a prioridade entre eventos do mesmo instante
// (menor primeiro); a ordem de agendamento desempata dentro do mesmo tipo.
enum class EventKind : uint8_t {
    VBlankIn,    // inicio do retraco vertical
    VBlankOut,   // fim do retraco vertical
    RenderDone,  // fim de renderizacao de um quadro
    InputDone,   // fim de uma transferencia de entrada (controles)
    FileDone,    // fim de uma etapa de leitura de arquivo/disco
    Timer,       // temporizador pedido por um cliente
    AudioTick,
    Count
};
constexpr const char* event_kind_name(EventKind k) {
    constexpr const char* kNames[] = {"VBlankIn", "VBlankOut", "RenderDone", "InputDone",
                                      "FileDone", "Timer",     "AudioTick"};
    return k < EventKind::Count ? kNames[static_cast<int>(k)] : "?";
}

struct EventTag;
using EventId = Handle<EventTag>;
struct SubscriptionTag;
using SubscriptionId = Handle<SubscriptionTag>;

struct Event {
    EventId   id;
    EventKind kind    = EventKind::Timer;
    Instant   due;           // instante devido
    uint64_t  payload = 0;   // dado do produtor
};

class IEventQueue {
public:
    virtual ~IEventQueue() = default;
    using Handler = std::function<void(const Event&, Instant delivered)>;

    // Produtores: agenda um evento tipado. Um instante ja passado e entregue no proximo
    // run_until.
    virtual EventId post(EventKind kind, Instant due, uint64_t payload = 0) = 0;
    // Remove um evento pendente; false se ja foi entregue ou cancelado.
    virtual bool cancel(EventId id) = 0;
    // Muda o instante de um evento pendente (mesmo id; para o desempate conta como agendado
    // agora); false se ja foi entregue ou cancelado.
    virtual bool reschedule(EventId id, Instant due) = 0;
    virtual bool pending(EventId id) const = 0;
    virtual std::optional<Instant> next_due() const = 0;

    // Consumidores: chamados na entrega, na ordem de inscricao. Cancelar uma inscricao vale na
    // hora, inclusive durante uma entrega; uma inscricao feita durante a entrega vale a partir do
    // proximo evento.
    virtual SubscriptionId subscribe(EventKind kind, Handler h) = 0;
    virtual void           unsubscribe(SubscriptionId id) = 0;
    // Clientes: temporizador de uma vez (EventKind::Timer), com o proprio callback.
    virtual EventId schedule_timer(Instant at, std::function<void(Instant)> cb) = 0;

    // Prioridade entre tipos no mesmo instante (menor primeiro). Padrao: a ordem de EventKind.
    virtual void set_kind_priority(EventKind kind, uint8_t priority) = 0;

    // Entrega, em ordem (instante devido, prioridade do tipo, ordem de agendamento), todo
    // evento com instante <= until. Eventos agendados durante a entrega entram na mesma
    // passagem se vencerem ate until. Devolve quantos foram entregues.
    virtual size_t run_until(Instant until) = 0;
};

// N2b (D4 revista): progresso logico e pontos de espera. A interface NAO escolhe nem alterna
// contextos de execucao: so observa o trabalho feito e as esperas e diz se o tempo logico
// poderia andar. Quem troca de contexto continua sendo o codigo que ja o faz (na transicao, o
// despachante do SDK recompilado). Um contexto e identificado por quem chama.
struct ContextTag;
using ContextId = Handle<ContextTag>;
struct WaitTag;
using WaitId = Handle<WaitTag>;

enum class ProgressDecision : uint8_t {
    Continue,     // ha evento vencido ainda nao entregue: entregar e continuar, sem andar o tempo
    AdvanceTime,  // o contexto so espera: o tempo pode ir ate `advance_to` (proximo evento)
    Deadlock,     // o contexto espera e nao ha evento futuro: nada vai libera-lo
};

struct ProgressReport {
    ProgressDecision decision = ProgressDecision::Continue;
    Instant          advance_to;
};

class IProgress {
public:
    virtual ~IProgress() = default;
    // Trabalho feito por `ctx` desde a ultima observacao, em unidades estruturais de progresso
    // (pontos de controle executados), nunca em tempo.
    virtual void work(ContextId ctx, uint64_t units) = 0;
    // `ctx` chegou (mais uma vez) ao ponto de espera `wait`, cuja condicao ainda e falsa.
    virtual ProgressReport at_wait(ContextId ctx, WaitId wait) = 0;
    // A condicao de `wait` ficou verdadeira: `ctx` saiu da espera.
    virtual void wait_done(ContextId ctx, WaitId wait) = 0;
    // Um evento foi entregue no instante `at`.
    virtual void delivered(EventKind kind, Instant at) = 0;
};

} // namespace bs::native::time
