#pragma once
// Incluido por todo arquivo gerado por bs-recomp. Macros de fluxo usadas pelo
// gerador; `c` e sempre o Sh4Context* da funcao corrente.
#include "cpu/sh4_ops.h"
#include "cpu/sh4_intc.h"
#include "cpu/sh4_runtime.h"

// Entrada de funcao: rastreabilidade endereco SH-4 <-> funcao C++ (custo: um teste).
// c->cur_fn (funcao corrente, salva/restaurada por BS_CALL) so e lido por diagnostico: a sonda
// de hardware do N0 (debug/hw_probe) atribui cada acesso a funcao do guest que o fez.
#define BS_FN_ENTER(addr)                                    \
    do {                                                     \
        c->cur_fn = (addr);                                  \
        if (::bs::g_sh4_trace) ::bs::g_sh4_trace(c, (addr)); \
    } while (0)
// Ciclos estimados de um trecho linear.
#define BS_CYCLES(n) (c->cycles += (n))
// Ponto de checagem de eventos (back-edges): interrupcoes, VBlank, timers.
// `resume` e o endereco SH-4 onde a execucao continua (vira SPC se houver interrupcao).
#define BS_POLL(resume)                       \
    do {                                      \
        if (c->cycles >= c->next_event) {     \
            c->pc = (resume);                 \
            ::bs::sh4_service(c);             \
        }                                     \
    } while (0)
// Depois de `ldc ...,sr`: uma interrupcao pendente pode ter sido desmascarada agora.
#define BS_IRQ_CHECK(resume)        \
    do {                            \
        c->pc = (resume);           \
        ::bs::sh4_irq_check(c);     \
    } while (0)
// Chamada: registra o retorno esperado para verificar o rts do chamado. No retorno ha
// um ponto de poll (alem dos back-edges): trechos longos sem laco, mas com chamadas,
// nao passam muito do quantum sem checar interrupcoes.
#define BS_CALL(call_expr, ret)                     \
    do {                                            \
        const uint32_t saved_ret_ = c->ret_expect;  \
        const uint32_t saved_sp_  = c->ret_sp;      \
        const uint32_t saved_fn_  = c->cur_fn;      \
        c->ret_expect             = (ret);          \
        c->ret_sp                 = c->r[15];       \
        call_expr;                                  \
        c->ret_expect = saved_ret_;                 \
        c->ret_sp     = saved_sp_;                  \
        c->cur_fn     = saved_fn_;                  \
        if (c->redirect) return;                    \
        BS_POLL(ret);                               \
    } while (0)
// Um longjmp pendente (c->redirect) sobe pelos quadros ate o da funcao que chamou o
// setjmp (BS_CALL acima so retorna). Numa funcao com pontos de reentrada, o longjmp
// volta a este quadro se o R15 restaurado e o do momento da chamada (o mesmo do
// setjmp, no mesmo quadro); a funcao entao salta para o rotulo do retorno do setjmp.
#define BS_CALL_REENTRANT(call_expr, ret)               \
    do {                                                \
        const uint32_t saved_ret_ = c->ret_expect;      \
        const uint32_t saved_sp_  = c->ret_sp;          \
        const uint32_t saved_fn_  = c->cur_fn;          \
        const uint32_t call_sp_   = c->r[15];           \
        c->ret_expect             = (ret);              \
        c->ret_sp                 = call_sp_;           \
        call_expr;                                      \
        c->ret_expect = saved_ret_;                     \
        c->ret_sp     = saved_sp_;                      \
        c->cur_fn     = saved_fn_;                      \
        if (c->redirect) {                              \
            if (c->r[15] == call_sp_) goto bs_reenter_; \
            return;                                     \
        }                                               \
        BS_POLL(ret);                                   \
    } while (0)
// rts de uma funcao longjmp (config [threads]): alvo diferente do esperado e troca de
// contexto do guest.
#define BS_LONGJMP_RET(pc, tgt)                                                 \
    do {                                                                        \
        if ((tgt) != c->ret_expect) ::bs::sh4_longjmp_redirect(c, (pc), (tgt)); \
        return;                                                                 \
    } while (0)
// rts/rte: o alvo (PR ou SPC, lido antes do delay slot) precisa ser o que o
// chamador espera. Ao invocar um tratador de excecao/interrupcao, o runtime
// define ret_expect = SPC.
#define BS_RET(pc, tgt)                                                  \
    do {                                                                 \
        if ((tgt) != c->ret_expect) ::bs::sh4_bad_return(c, (pc), (tgt)); \
        return;                                                          \
    } while (0)
// rte: SPC diferente do esperado e uma continuacao (o tratador redirecionou a volta da
// interrupcao, p.ex. para rodar uma rotina adiada em 8C17DE4C). O runtime despacha o
// alvo mantendo ret_expect; o rte final, de volta ao ponto interrompido, desenrola.
#define BS_RTE(pc, tgt)                                                         \
    do {                                                                        \
        if ((tgt) != c->ret_expect) ::bs::sh4_rte_redirect(c, (pc), (tgt));     \
        return;                                                                 \
    } while (0)
// Alvo resolvido estaticamente: confere em tempo de execucao.
#define BS_EXPECT_TARGET(pc, got, want)                                         \
    do {                                                                        \
        if (::bs::sh4_phys(got) != ::bs::sh4_phys(want)) ::bs::sh4_dispatch_miss(c, (pc), (got)); \
    } while (0)
