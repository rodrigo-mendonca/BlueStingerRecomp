#pragma once
// N2a (docs/N2-PLANO.md): provedor de tempo do runtime. Os modulos (Holly, PVR, Maple, GD-ROM,
// TMU, RTC) nao calculam mais tempo a partir do contador de ciclos: pedem a um provedor
//  - os eventos agendados vencidos (VBlank-In/Out, fim de render, fim de DMA do Maple, bloco
//    de leitura do GD-ROM), entregues nos mesmos pontos de antes;
//  - o instante atual (relogio consultavel: TMU, SPG_STATUS, MDST) e a hora do RTC.
// Provedores:
//  - legado (cpu/time_legacy.cpp): o calculo preguicoso por ciclos de antes (referencia e
//    rollback, --tempo legado);
//  - nativo (transitional/time_adapter): relogio logico e fila de eventos de native/, com os
//    ciclos estimados so como entrada de compatibilidade.
// Durante o N2a a base de progresso continua sendo o contador de ciclos (BS_CYCLES/BS_POLL):
// time_now() e o unico ponto em que um modulo le c->cycles para medir tempo.
#include "cpu/sh4_context.h"

#include <cstdint>

namespace bs {

// Base de tempo de compatibilidade: um tick por ciclo estimado do SH-4 a 200 MHz; um campo
// NTSC = 200 MHz * 1001/60000 ciclos (truncado, como sempre foi).
constexpr uint64_t kCompatTicksPerSecond = 200000000;
constexpr uint64_t kCompatTicksPerField  = kCompatTicksPerSecond * 1001 / 60000;

// Eventos do Holly vencidos (resultado de holly_due).
constexpr uint32_t kDueVblankIn  = 1u << 0;
constexpr uint32_t kDueVblankOut = 1u << 1;
constexpr uint32_t kDueRender    = 1u << 2;
constexpr uint32_t kDueMaple     = 1u << 3;

class TimeProvider {
public:
    virtual ~TimeProvider() = default;
    virtual const char* name() const = 0;
    // Sessao nova no instante `now` (holly_intc_reset).
    virtual void reset(uint64_t now) = 0;
    // Relogio consultavel: instante atual para quem le contadores (TMU, SPG, Maple).
    virtual uint64_t clock(uint64_t cycles) = 0;

    // Eventos do Holly vencidos em (ultimo, now]; chamado so com now maior que o ultimo.
    virtual uint32_t holly_due(uint64_t now) = 0;
    // A configuracao do SPG mudou (linhas, VBlank, fase): vale do ultimo instante entregue em diante.
    virtual void spg_changed() = 0;
    virtual void render_scheduled(uint64_t due) = 0;  // STARTRENDER: fim de render em `due`
    virtual void maple_scheduled(uint64_t due) = 0;   // DMA do Maple: fim em `due`
    virtual void maple_cancel() = 0;                  // fim visto antes pelo controlador

    virtual void disc_scheduled(uint64_t due) = 0;    // proximo bloco de leitura do GD-ROM em `due`
    virtual bool disc_due(uint64_t now) = 0;          // o bloco venceu ate `now` (consome)
    virtual void disc_cancel() = 0;

    // RTC (segundos na epoca do Dreamcast, 1950)
    virtual uint32_t rtc_seconds(uint64_t now) = 0;
    virtual void     rtc_set(uint32_t seconds, uint64_t now) = 0;

    // N2b-1 (modo sombra, so observacao): progresso estrutural da thread `thread` (contador de
    // pontos de controle `polls`, monotono) e pontos de espera declarados. Nao mudam a execucao.
    virtual void progress(uint32_t thread, uint64_t polls) { (void)thread, (void)polls; }
    virtual void wait_point(uint32_t pc, uint32_t thread, uint64_t polls) { (void)pc, (void)thread, (void)polls; }
    virtual void wait_end(uint32_t pc, uint32_t thread, uint64_t polls) { (void)pc, (void)thread, (void)polls; }
    // Diagnostico (BS_TMU_DIAG=agendado): instante devido do ultimo VBlank-In entregue (0 se
    // o provedor nao sabe).
    virtual uint64_t last_vblank_due() const { return 0; }
};

// Provedor atual (nunca nulo: o legado e o padrao).
TimeProvider& time_provider();
// Troca o provedor (nullptr volta ao legado). Chamar antes de boot_prepare.
void time_set_provider(TimeProvider* p);
// Provedor legado (testes e comparacao).
TimeProvider& time_legacy_provider();

// Instante atual visto pelos modulos (entrada de compatibilidade: o contador de ciclos).
inline uint64_t time_now(const Sh4Context* c) { return time_provider().clock(c->cycles); }

} // namespace bs
