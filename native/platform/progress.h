#pragma once
// N2b (D4, D8): monitor de progresso logico (IProgress). Observa o trabalho dos contextos e os
// pontos de espera e responde se o tempo logico poderia andar. Nao escolhe nem troca
// contextos. No N2b-1 (modo sombra) so registra: a execucao real nao muda.
//
// Janela de trabalho: o trabalho de todos os contextos desde a ultima chegada de qualquer
// contexto a um ponto de espera. E o que importa para o N2b: o tempo so pode andar quando o
// contexto corrente espera; um contexto que cede a vez a outro (sem esperar) nao abre caminho
// para o tempo. Falta de progresso (D8): janela acima de `stall_units`, marcada uma vez, com o
// contexto que mais trabalhou nela.
//
// Episodio de espera: da primeira chegada de um contexto a um ponto de espera ate a saida dele
// (wait_done). Guarda as voltas, os eventos entregues no meio, o ultimo deles antes da saida e
// a duracao logica. Saida sem evento entregue no meio e anomalia: no N2b-2 a espera ficaria
// presa. Uma espera pode declarar o evento que a libera (`expect`): a saida conta como "com o
// evento esperado" so se ele foi entregue no episodio. Quem chama `delivered` deve faze-lo so
// quando o evento chegou de fato ao hardware (um evento descartado nao libera nada). Isso prova
// a entrega no episodio, nao a transicao da condicao no jogo.
#include "platform/time.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>

namespace bs::native::time {

class ProgressMonitor final : public IProgress {
public:
    static constexpr size_t kKinds = static_cast<size_t>(EventKind::Count);

    struct ContextStats {
        uint64_t total = 0;            // trabalho total
        uint64_t waits = 0;            // chegadas a pontos de espera (voltas)
        uint64_t stalls_blamed = 0;    // janelas acima do limite em que foi o que mais trabalhou
        // episodio aberto
        bool     in_wait = false;
        WaitId   wait;
        uint64_t iterations = 0;
        uint32_t delivered_mask = 0;   // tipos entregues no episodio (bit = EventKind)
        int      last_kind = -1;
        Instant  last_target;
        uint64_t advances = 0;         // alvos distintos para os quais o tempo andaria
        Instant  begin;                // inicio do episodio
    };
    struct WaitStats {
        uint64_t episodes = 0, iterations = 0, advances = 0, deadlocks = 0;
        uint64_t exits_with_event = 0, exits_without_event = 0;
        std::array<uint64_t, kKinds> released_by{};  // ultimo tipo entregue antes da saida
        int      expected = -1;  // EventKind que libera a espera (-1: nao declarado)
        uint64_t exits_with_expected = 0, exits_without_expected = 0;
        uint64_t max_ticks = 0;  // maior duracao logica de um episodio (ate a saida)
    };
    // Window: uma janela de trabalho fechou acima de `set_window_report` (calibracao do D8).
    enum class RecordKind : uint8_t { EpisodeBegin, Advance, EpisodeEnd, Stall, Deadlock, Window };
    struct Record {
        RecordKind kind;
        ContextId  ctx;
        WaitId     wait;
        Instant    now;
        Instant    target;          // Advance: alvo; EpisodeEnd: inicio do episodio
        uint64_t   value = 0;       // EpisodeEnd: voltas; Stall: trabalho na janela
        uint32_t   delivered_mask = 0;
        int        released_by = -1;
    };
    using Observer = std::function<void(const Record&)>;

    ProgressMonitor(const IClock& clock, const IEventQueue& queue, uint64_t stall_units);

    void           work(ContextId ctx, uint64_t units) override;
    ProgressReport at_wait(ContextId ctx, WaitId wait) override;
    void           wait_done(ContextId ctx, WaitId wait) override;
    void           delivered(EventKind kind, Instant at) override;
    // `n` voltas a mais no ponto de espera em que `ctx` ja esta, sem trabalho nem evento no meio
    // (equivale a n chamadas de at_wait com a mesma decisao; quem chama acumula para nao pagar
    // uma chamada por volta).
    void           repeat_wait(ContextId ctx, WaitId wait, uint64_t n);
    // evento que libera `wait` (para as contagens de saida com o evento esperado)
    void           expect(WaitId wait, EventKind kind) { waits_[wait.value].expected = static_cast<int>(kind); }

    void set_observer(Observer o) { observer_ = std::move(o); }
    void set_window_report(uint64_t units) { windowReport_ = units; }  // 0 = nao registra
    const std::map<uint32_t, ContextStats>& contexts() const { return ctx_; }
    const std::map<uint32_t, WaitStats>&    waits() const { return waits_; }
    uint64_t window() const { return window_; }               // trabalho desde a ultima espera
    uint64_t max_window() const { return maxWindow_; }        // maior janela vista
    uint64_t stalls() const { return stalls_; }

private:
    void endEpisode(ContextStats& c, ContextId ctx, bool exited);

    const IClock&                    clock_;
    const IEventQueue&               queue_;
    uint64_t                         stallUnits_;
    std::map<uint32_t, ContextStats> ctx_;
    std::map<uint32_t, WaitStats>    waits_;
    std::map<uint32_t, uint64_t>     windowBy_;  // trabalho de cada contexto na janela
    uint64_t                         window_ = 0, maxWindow_ = 0, stalls_ = 0;
    bool                             stalled_ = false;
    uint64_t                         windowReport_ = 0;
    Observer                         observer_;
};

} // namespace bs::native::time
