#include "time_adapter.h"

#include "cri_idle.h"

#include "cpu/sh4_intc.h"
#include "cpu/sh4_runtime.h"
#include "cpu/time_hooks.h"
#include "debug/intc_trace.h"
#include "platform/clock.h"
#include "platform/event_queue.h"
#include "platform/progress.h"
#include "pvr/holly_intc.h"
#include "pvr/pvr_regs.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include <vector>
#include <algorithm>
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
// Observador de passos de jogo (diagnostico do host: estado logico por passo, BS_STATE_PASSO)
void (*g_stepObserver)(uint64_t step, void* ctx) = nullptr;
void* g_stepCtx = nullptr;

// N2b-2 (--tempo experimental): o que libera cada ponto de espera declarado ([native] esperas no
// config; inventario em docs/N2b-ESPERAS.md). Ponto nao listado (p.ex. bs::mods::wait num mod) e
// tratado como Evento.
//  - Evento: o tempo anda ate o proximo evento das filas (VBlank, render, Maple, GD-ROM); os
//    timeouts pelo TMU vencem porque o TMU le o mesmo instante;
//  - LinhaSpg: o laco le SPG_STATUS, que so muda na troca de linha do feixe: o tempo anda ate a
//    proxima troca de linha ou o proximo evento, o que vier antes;
//  - Sincrona: a condicao e satisfeita na propria escrita (DMA ch2, FIFO do G2); o laco nao pode
//    dar volta. Se der, e erro (nao ha evento que a libere);
//  - OciosaCri: back-edge da thread 3; so espera se a passada foi ociosa (cri_idle.h).
// `n2b2`: ponto novo do N2b-2, ignorado no tempo nativo estavel (o modo padrao nao muda).
enum class WaitKind : uint8_t { Event, SpgLine, Sync, CriIdle };
struct WaitDecl {
    uint32_t    pc;
    WaitKind    kind;
    bool        n2b2;
    const char* what;
};
constexpr WaitDecl kWaitDecls[] = {
    {0x8C064A2C, WaitKind::Event, false, "VBlank do jogo (bandeira 0x8C24F88A)"},
    {0x8C173086, WaitKind::Event, false, "fim do render anterior, timeout de 166 ms pelo TMU"},
    {0x8C0978F6, WaitKind::Event, false, "bit com timeout pelo TMU (8C0978BE)"},
    {0x8C172628, WaitKind::SpgLine, true, "boot: vsync do SPG acender (f_8C172620)"},
    {0x8C17262E, WaitKind::SpgLine, true, "boot: vsync do SPG apagar (f_8C172620)"},
    {0x8C172808, WaitKind::Event, true, "boot: VBlank-In em ISTNRM (f_8C172642)"},
    {0x8C172EF8, WaitKind::Event, true, "boot: contador do callback de VBlank 8C172602 (f_8C172D22)"},
    {0x8C1C6D06, WaitKind::SpgLine, true, "boot: linha do SPG sair de 0 (f_8C1C6B3C)"},
    {0x8C1C6D0E, WaitKind::SpgLine, true, "boot: linha do SPG voltar a 0 (f_8C1C6B3C)"},
    {0x8C1C1DC6, WaitKind::Sync, true, "DMA ch2: C2DST = 0 (conclui na escrita)"},
    {0x8C096EE8, WaitKind::Sync, true, "FIFO do G2 vazia: SB_FFST bit 0 (conclui na escrita)"},
    {0x8C096EF8, WaitKind::Sync, true, "FIFO do G2 vazia: SB_FFST bit 0 (conclui na escrita)"},
    {0x8C097254, WaitKind::Sync, true, "FIFO do G2 vazia: SB_FFST bit 5 (conclui na escrita)"},
    {0x8C097264, WaitKind::Sync, true, "FIFO do G2 vazia: SB_FFST bit 5 (conclui na escrita)"},
    {0x8C158860, WaitKind::CriIdle, true, "thread 3: passada do servidor do CRI sem efeito"},
};
const WaitDecl* findWait(uint32_t pc) {
    for (const WaitDecl& w : kWaitDecls)
        if (w.pc == pc) return &w;
    return nullptr;
}
constexpr size_t   kRamBytes  = size_t(16) << 20;
constexpr uint64_t kNoTarget  = ~0ull;
constexpr uint64_t kMaxDueNow = 1000;  // eventos vencidos seguidos numa espera sem entrega: erro

class NativeTime final : public TimeProvider {
public:
    explicit NativeTime(const Options& opt) : stallUnits_(opt.stall_units), exp_(opt.experimental) {
        const std::string& m = opt.mutation;
        if ((m == "cri_ignora_ram" || m == "cri_nunca_ocioso" || m == "cri_sempre_ocioso") && !exp_)
            throw std::invalid_argument("BS_TEMPO_MUTACAO=" + m + " exige --tempo experimental");
        if (m == "cri_ignora_ram") criIgnoreRam_ = true;
        else if (m == "cri_nunca_ocioso") criNeverIdle_ = true;
        else if (m == "cri_sempre_ocioso") criAlwaysIdle_ = true;
        else if (m == "perder_vblank_out") dropVblankOut_ = true;
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
        if (exp_) {
            if (!opt.ram) throw std::invalid_argument("tempo experimental sem RAM com rastreamento de escrita");
            if (!stallUnits_) throw std::invalid_argument("tempo experimental exige o limite de falta de progresso (D8)");
            cri_ = std::make_unique<cri::IdleDetector>(opt.ram, kRamBytes, criIgnoreRam_);
        }
        start(0);
    }
    ~NativeTime() override {
        if (trace_) std::fclose(trace_);
        if (ptrace_) std::fclose(ptrace_);
    }

    const char* name() const override { return exp_ ? "experimental" : "nativo"; }

    // N2b-2: uma sessao nova do hardware nao volta o instante logico (ele nao vem dos ciclos)
    void reset(uint64_t now) override { start(exp_ ? clock_->now().ticks : now); }

    uint64_t clock(uint64_t cycles) override {
        if (exp_) return clock_->now().ticks;  // N2b-2: os ciclos nao movem o tempo
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
        if (profile_ && thread < kMaxThreads && units) prof_[thread][pc] += units;  // BS_PROGRESSO_PCS (inventario P0)
        if (progress_->stalls() != lastStalls_) {
            // D8 (modo sombra: aviso; no N2b-2, erro): a primeira falta, com o trabalho de cada
            // thread na janela e o ultimo PC dela (a que cruzou o limite nao e necessariamente a
            // culpada: no N2b-1 a thread 3 nunca passa por um ponto de espera)
            if (!lastStalls_ || exp_) {
                std::string who;
                for (const auto& [id, w] : progress_->window_by()) {
                    const uint32_t t   = id - 1;
                    uint32_t       top = 0;
                    uint64_t       most = 0;
                    if (t < kMaxThreads)
                        for (const auto& [p, u] : hot_[t])
                            if (u > most) most = u, top = p;
                    char b[160];
                    std::snprintf(b, sizeof b, "%sthread %u %llu (maior lote em %08X", who.empty() ? "" : ", ", t, ull(w), top);
                    who += b;
                    if (exp_ && t < kMaxThreads && lastWait_[t]) {
                        std::snprintf(b, sizeof b, "; ultima espera %08X", lastWait_[t]);
                        who += b;
                    }
                    who += ")";
                }
                if (exp_) {  // N2b-2 (D8): erro, nunca avanco silencioso do tempo
                    char b[256];
                    std::snprintf(b, sizeof b,
                                  "tempo experimental: falta de progresso: %llu pontos de poll sem chegar a um ponto de "
                                  "espera (limite %llu); thread corrente %u em %08X; na janela: ",
                                  ull(progress_->window()), ull(stallUnits_), thread, pc);
                    throw std::runtime_error(b + who + ". Um laco que espera hardware ou evento precisa ser declarado em "
                                                       "[native] esperas (ou usar bs::mods::wait() num mod)");
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
        const WaitDecl* d = findWait(pc);
        if (!exp_ && d && d->n2b2) return;  // modo estavel: as esperas do N2b-2 nao existem
        if (pc == kVblankWaitPc) {
            ++stats.gameSteps;
            if (g_intc_trace) intc_trace_step(stats.gameSteps);
            if (g_stepObserver) g_stepObserver(stats.gameSteps, g_stepCtx);
        }
        progress(thread, polls, pc);
        if (skipWait_ && pc == kVblankWaitPc) return;
        progress_->wait_done(nt::ContextId{thread + 1}, nt::WaitId{pc});
    }

    bool wait(Sh4Context* c, uint32_t pc, uint32_t thread) override {
        const WaitDecl* d = findWait(pc);
        if (!exp_) {
            if (d && d->n2b2) return false;  // modo estavel: as esperas do N2b-2 nao existem
            wait_point(pc, thread, c->polls);
            return false;
        }
        return experimentalWait(c, pc, thread, d ? d->kind : WaitKind::Event, d ? d->what : "espera sem declaracao de tipo");
    }

    bool holly_pending(uint64_t now) override {
        if (!exp_) return false;
        const auto next = display_.next_due();
        return next && next->ticks <= now;
    }
    uint64_t last_vblank_due() const override { return lastVbDue_; }

    std::string progress_summary() {
        flushRepeats();
        std::string out;
        char        b[256];
        for (uint32_t t = 0; profile_ && t < kMaxThreads; ++t) {  // BS_PROGRESSO_PCS: onde o trabalho foi gasto
            if (prof_[t].empty()) continue;
            std::vector<std::pair<uint64_t, uint32_t>> v;
            uint64_t                                    total = 0;
            for (const auto& [pc, u] : prof_[t]) v.push_back({u, pc}), total += u;
            std::sort(v.rbegin(), v.rend());
            const char* path = std::getenv("BS_PROGRESSO_PCS");  // um caminho: perfil completo no arquivo
            if (path && std::strlen(path) > 1)
                if (std::FILE* f = std::fopen(path, t == 0 ? "w" : "a")) {
                    for (const auto& [u, pc] : v) std::fprintf(f, "%u\t%08X\t%llu\n", t, pc, ull(u));
                    std::fclose(f);
                }
            for (size_t i = 0; i < v.size() && i < 40; ++i) {
                std::snprintf(b, sizeof b, "[progresso] pcs thread %u: %08X %llu (%.2f%%)\n", t, v[i].second, ull(v[i].first),
                              100.0 * double(v[i].first) / double(total));
                out += b;
            }
        }
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
        if (exp_) {
            const ExpStats&      e = exp_stats_;
            const cri::IdleStats s = cri_->stats();
            std::snprintf(b, sizeof b,
                          "[n2b2] tempo experimental: instante final %llu ticks (%.2f campos); avancos %llu (linha do "
                          "SPG %llu), maior passo %llu ticks, eventos vencidos entregues sem avanco %llu\n",
                          ull(clock_->now().ticks), double(clock_->now().ticks) / double(kCompatTicksPerField),
                          ull(e.advances), ull(e.spgLine), ull(e.maxStep), ull(e.dueNow));
            out += b;
            std::snprintf(b, sizeof b,
                          "[n2b2] thread 3: visitas %llu, ociosas (bloqueios) %llu, com evento %llu, com efeito na RAM "
                          "%llu, com outro efeito %llu; paginas escritas comparadas %llu\n",
                          ull(s.visits), ull(e.criBlocks), ull(s.eventDuring), ull(s.effectRam), ull(s.effectOther),
                          ull(s.dirtyPages));
            out += b;
            if (e.hwWaits) {
                std::snprintf(b, sizeof b, "[n2b2] esperas do hardware (DMA): %llu, %llu avancos, %.2f campos\n", ull(e.hwWaits),
                              ull(e.hwWaitSteps), double(e.hwWaitTicks) / double(kCompatTicksPerField));
                out += b;
            }
            for (const auto& [pc, n] : e.byWait) {
                const WaitDecl* d = findWait(pc);
                std::snprintf(b, sizeof b, "[n2b2] avancos na espera %08X: %llu (%s)\n", pc, ull(n), d ? d->what : "sem declaracao");
                out += b;
            }
        }
        return out;
    }

    Stats stats;

    bool experimental() const { return exp_; }

    // N2 (11.1 no experimental, 2026-10-23): o SH-4 parado enquanto o hardware trabalha (o DMA do download da musica).
    // Nao e trabalho da CPU (D11 vale para o trabalho): e tempo de hardware, entao o instante logico anda `ticks`, de
    // evento em evento (VBlank, render, Maple, GD-ROM), com a entrega no caminho, como a espera do tempo estavel faz com
    // os ciclos. Antes o experimental devolvia na hora e os dois lotes do driver de som de uma troca de musica caiam no
    // mesmo tick (sem o fade de entrada do original). So um longjmp (c->redirect) encerra a espera.
    void timed_wait(Sh4Context* c, uint64_t ticks) {
        const uint64_t end = clock_->now().ticks + ticks;
        c->pc              = c->pr;
        uint32_t stuck     = 0;
        while (clock_->now().ticks < end && !c->redirect) {
            const uint64_t now    = clock_->now().ticks;
            uint64_t       target = end;
            if (const auto n = display_.next_due()) target = std::min(target, n->ticks);
            if (const auto n = disc_.next_due()) target = std::min(target, n->ticks);
            if (target > now) {
                clock_->advance_to(nt::Instant{target});
                ++exp_stats_.advances, ++exp_stats_.hwWaitSteps;
                exp_stats_.ticks += target - now, exp_stats_.hwWaitTicks += target - now;
            }
            ++c->polls;
            sh4_irq_origin_hint(uint8_t(IntcSource::Hardware));  // diagnostico BS_INTC_TRACE
            sh4_service(c);  // entrega o que venceu (o tratador roda aninhado; a troca de thread suspende esta fibra)
            // termino garantido: atendimentos seguidos sem o relogio andar tem limite, mesmo que as filas mudem (eventos
            // distintos no mesmo instante sao poucos; kMaxDueNow fica muito acima). Revisoes Codex 2026-10-23 e -24.
            if (target > now) stuck = 0;
            else if (++stuck > kMaxDueNow) {
                char b[200];
                std::snprintf(b, sizeof b, "tempo experimental: evento vencido em %llu nao e entregue durante a espera do "
                                           "hardware (retorno %08X)", ull(target), c->pr);
                throw std::runtime_error(b);
            }
        }
        ++exp_stats_.hwWaits;
    }

private:
    static unsigned long long ull(uint64_t v) { return static_cast<unsigned long long>(v); }

    // N2b-2: o contexto corrente chegou ao ponto de espera `pc` e a condicao do laco ainda e falsa
    // (senao ele teria saido). Devolve true se o instante andou ou ha evento vencido a entregar.
    bool experimentalWait(Sh4Context* c, uint32_t pc, uint32_t thread, WaitKind kind, const char* what) {
        if (skipWait_ && pc == kVblankWaitPc) {  // mutacao: espera nao declarada (a guarda tem de pegar)
            progress(thread, c->polls, pc);
            return false;
        }
        progress(thread, c->polls, pc);
        if (thread < kMaxThreads) lastWait_[thread] = pc;
        char b[320];
        if (kind == WaitKind::Sync) {
            std::snprintf(b, sizeof b,
                          "tempo experimental: a espera sincrona %08X (%s) deu volta na thread %u: a condicao devia "
                          "estar satisfeita na escrita e nenhum evento a libera",
                          pc, what, thread);
            throw std::runtime_error(b);
        }
        const bool criIdle = kind == WaitKind::CriIdle && (cri_->idle(c) || criAlwaysIdle_) && !criNeverIdle_;
        if (kind == WaitKind::CriIdle && !criIdle) {
            ++exp_stats_.criWork;  // a passada trabalhou (ou houve evento): a proxima roda ja
            return false;
        }
        if (kind == WaitKind::CriIdle) ++exp_stats_.criBlocks;
        // o tempo pode andar aqui: episodio de espera e janela de trabalho no monitor
        progress_->at_wait(nt::ContextId{thread + 1}, nt::WaitId{pc});
        for (auto& h : hot_)
            if (!h.empty()) h.clear();
        const uint64_t now    = clock_->now().ticks;
        uint64_t       target = kNoTarget;
        if (const auto n = display_.next_due()) target = n->ticks;
        if (const auto n = disc_.next_due()) target = std::min(target, n->ticks);
        bool line = false;
        if (kind == WaitKind::SpgLine) {
            const uint64_t l = pvr_spg_next_line_change(now);
            if (l != kSpgNever && l < target) target = l, line = true;
        }
        if (target == kNoTarget) {
            std::snprintf(b, sizeof b,
                          "tempo experimental: deadlock na espera %08X (%s), thread %u: nenhum evento agendado", pc,
                          what, thread);
            throw std::runtime_error(b);
        }
        if (target <= now) {  // evento vencido ainda nao entregue: entregar antes de andar o tempo
            if (++dueNowRun_ > kMaxDueNow) {
                std::snprintf(b, sizeof b,
                              "tempo experimental: evento vencido em %llu nao e entregue na espera %08X (%s), thread %u",
                              ull(target), pc, what, thread);
                throw std::runtime_error(b);
            }
            ++exp_stats_.dueNow;
            return true;
        }
        dueNowRun_ = 0;
        clock_->advance_to(nt::Instant{target});
        ++exp_stats_.advances;
        exp_stats_.spgLine += line;
        exp_stats_.ticks += target - now;
        if (target - now > exp_stats_.maxStep) exp_stats_.maxStep = target - now;
        ++exp_stats_.byWait[pc];
        return true;
    }

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
        dueNowRun_ = 0;  // N2b-2: o evento vencido foi entregue (o limite mede so voltas seguidas sem entrega)
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
    const bool profile_ = std::getenv("BS_PROGRESSO_PCS") != nullptr;  // diagnostico: perfil de trabalho por PC na execucao toda
    std::unordered_map<uint32_t, uint64_t> prof_[kMaxThreads];
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
    // N2b-2 (--tempo experimental)
    bool                               exp_          = false;
    bool                               criIgnoreRam_ = false, criNeverIdle_ = false, criAlwaysIdle_ = false;
    std::unique_ptr<cri::IdleDetector> cri_;
    uint32_t                           lastWait_[kMaxThreads] = {};
    uint64_t                           dueNowRun_ = 0;
    struct ExpStats {
        uint64_t advances = 0, spgLine = 0, dueNow = 0, ticks = 0, maxStep = 0, criWork = 0, criBlocks = 0;
        uint64_t hwWaits = 0, hwWaitSteps = 0, hwWaitTicks = 0;  // timed_wait (DMA da musica)
        std::map<uint32_t, uint64_t> byWait;  // avancos do tempo por ponto de espera
    } exp_stats_;
};

// N2 (2026-10-23): --tempo legado com o contador de passos de jogo. Tudo e do provedor legado; so a saida da espera do
// VBlank do jogo e contada, como no nativo. Sem ele, game_steps() ficava em 0 no legado e as falas nativas (que andam
// por passo de jogo) se comportavam diferente conforme o provedor de tempo (test_tempo A1: 3 ciclos de diferenca no
// VBlank ~2270 com o audio nativo).
class LegacySteps final : public TimeProvider {
public:
    const char* name() const override { return l_.name(); }
    void        reset(uint64_t now) override { l_.reset(now); }
    uint64_t    clock(uint64_t cycles) override { return l_.clock(cycles); }
    uint32_t    holly_due(uint64_t now) override { return l_.holly_due(now); }
    void        spg_changed() override { l_.spg_changed(); }
    void        render_scheduled(uint64_t due) override { l_.render_scheduled(due); }
    void        maple_scheduled(uint64_t due) override { l_.maple_scheduled(due); }
    void        maple_cancel() override { l_.maple_cancel(); }
    void        disc_scheduled(uint64_t due) override { l_.disc_scheduled(due); }
    bool        disc_due(uint64_t now) override { return l_.disc_due(now); }
    void        disc_cancel() override { l_.disc_cancel(); }
    uint32_t    rtc_seconds(uint64_t now) override { return l_.rtc_seconds(now); }
    void        rtc_set(uint32_t seconds, uint64_t now) override { l_.rtc_set(seconds, now); }
    void        progress(uint32_t thread, uint64_t polls, uint32_t pc) override { l_.progress(thread, polls, pc); }
    void        wait_point(uint32_t pc, uint32_t thread, uint64_t polls) override { l_.wait_point(pc, thread, polls); }
    void        wait_end(uint32_t pc, uint32_t thread, uint64_t polls) override {
        if (pc == kVblankWaitPc) {
            ++steps;
            if (g_intc_trace) intc_trace_step(steps);
            if (g_stepObserver) g_stepObserver(steps, g_stepCtx);
        }
        l_.wait_end(pc, thread, polls);
    }
    uint64_t last_vblank_due() const override { return l_.last_vblank_due(); }
    bool     wait(Sh4Context* c, uint32_t pc, uint32_t thread) override { return l_.wait(c, pc, thread); }
    bool     holly_pending(uint64_t now) override { return l_.holly_pending(now); }
    uint64_t steps = 0;

private:
    TimeProvider& l_ = time_legacy_provider();
};

std::unique_ptr<NativeTime>  g_native;
std::unique_ptr<LegacySteps> g_legacy;
Stats                        g_lastStats;
std::string                  g_lastProgress;

} // namespace

void install(const Options& opt) {
    uninstall();
    g_native = std::make_unique<NativeTime>(opt);
    time_set_provider(g_native.get());
}

void install_legacy_steps() {
    uninstall();
    g_legacy = std::make_unique<LegacySteps>();
    time_set_provider(g_legacy.get());
}

void uninstall() {
    if (g_legacy) {
        time_set_provider(nullptr);
        g_lastStats           = {};
        g_lastStats.gameSteps = g_legacy->steps;
        g_legacy.reset();
    }
    if (!g_native) return;
    time_set_provider(nullptr);
    g_lastStats    = g_native->stats;
    g_lastProgress = g_native->progress_summary();
    g_native.reset();
}

bool installed() { return g_native != nullptr; }

Stats stats() { return g_native ? g_native->stats : g_lastStats; }

uint64_t game_steps() {
    return g_native ? g_native->stats.gameSteps : g_legacy ? g_legacy->steps : g_lastStats.gameSteps;
}

void set_step_observer(void (*fn)(uint64_t step, void* ctx), void* ctx) {
    g_stepObserver = fn;
    g_stepCtx      = ctx;
}

std::string progress_summary() { return g_native ? g_native->progress_summary() : g_lastProgress; }

bool hardware_wait(Sh4Context* c, uint64_t ticks) {
    if (g_native && g_native->experimental()) {  // o instante logico anda o tempo do hardware (nao e trabalho, D11)
        g_native->timed_wait(c, ticks);
        return true;
    }
    // como o laco de espera gerado: o tempo anda ate o proximo atendimento e os eventos vencidos (VBlank, render,
    // Maple, GD-ROM) sao entregues no caminho, com as interrupcoes (o retorno da chamada e o ponto de retomada). A
    // interrupcao e uma chamada aninhada (sh4_intc.cpp, take: o rte do tratador volta ao SPC) e a troca de thread
    // suspende esta fibra: a espera continua de onde parou, com o mesmo prazo. So um longjmp (c->redirect) a encerra;
    // o chamador sobe sem mexer nos registradores. `polls` conta um ponto por atendimento (observacao do progresso; o
    // laco original no SDK faria mais voltas, sem efeito no tempo estavel). (Revisao Codex 2026-10-23.)
    const uint64_t end = c->cycles + ticks;
    c->pc              = c->pr;
    while (c->cycles < end && !c->redirect) {
        c->cycles = std::min(end, std::max(c->cycles, c->next_event));
        ++c->polls;
        if (c->cycles >= c->next_event) {
            sh4_irq_origin_hint(uint8_t(IntcSource::Hardware));  // diagnostico BS_INTC_TRACE
            sh4_service(c);
        }
    }
    return true;
}

} // namespace bs::transitional::time
