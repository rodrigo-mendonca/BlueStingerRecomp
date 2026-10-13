#pragma once
// Incluido por todo arquivo gerado por bs-recomp. Macros de fluxo usadas pelo
// gerador; `c` e sempre o Sh4Context* da funcao corrente.
#include "cpu/sh4_ops.h"
#include "cpu/sh4_runtime.h"

// Entrada de funcao: rastreabilidade endereco SH-4 <-> funcao C++ (custo: um teste).
#define BS_FN_ENTER(addr)                                    \
    do {                                                     \
        if (::bs::g_sh4_trace) ::bs::g_sh4_trace(c, (addr)); \
    } while (0)
// Ciclos estimados de um trecho linear.
#define BS_CYCLES(n) (c->cycles += (n))
// Ponto de checagem de eventos (back-edges): interrupcoes, VBlank, timers.
#define BS_POLL()                                              \
    do {                                                       \
        if (c->cycles >= c->next_event) ::bs::sh4_service(c);  \
    } while (0)
// Chamada: registra o retorno esperado para verificar o rts do chamado.
#define BS_CALL(call_expr, ret)                     \
    do {                                            \
        const uint32_t saved_ret_ = c->ret_expect;  \
        c->ret_expect             = (ret);          \
        call_expr;                                  \
        c->ret_expect = saved_ret_;                 \
    } while (0)
// rts/rte: o alvo (PR ou SPC, lido antes do delay slot) precisa ser o que o
// chamador espera. Ao invocar um tratador de excecao/interrupcao, o runtime
// define ret_expect = SPC.
#define BS_RET(pc, tgt)                                                  \
    do {                                                                 \
        if ((tgt) != c->ret_expect) ::bs::sh4_bad_return(c, (pc), (tgt)); \
        return;                                                          \
    } while (0)
// Alvo resolvido estaticamente: confere em tempo de execucao.
#define BS_EXPECT_TARGET(pc, got, want)                                         \
    do {                                                                        \
        if (::bs::sh4_phys(got) != ::bs::sh4_phys(want)) ::bs::sh4_dispatch_miss(c, (pc), (got)); \
    } while (0)
