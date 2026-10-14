#pragma once
// Threads do guest sobre fibras do host. O codigo recompilado usa a pilha C do host
// para as chamadas do SH-4; um jogo com varias threads (o Blue Stinger troca de thread
// com `rte`, carregando SPC/SSR e R15 de outro contexto) precisa de uma pilha C por
// thread. Cada thread do guest roda numa fibra; a troca acontece num `rte` cujo alvo
// e R15 identificam outro contexto.
#include "cpu/sh4_context.h"

namespace bs {

// `rte` para um alvo diferente do esperado (BS_RTE). Com o R15 ja restaurado pelo
// codigo do guest:
//  - alvo/R15 de uma thread suspensa: suspende a atual e retoma aquela;
//  - mesmo R15 esperado: continuacao na mesma thread (rotina adiada do tratador de
//    interrupcao), despachada na pilha atual;
//  - senao: thread nova, numa fibra nova que comeca em `target`.
// A thread suspensa volta exatamente deste ponto, como se o rte tivesse ido ao alvo
// esperado (o guest restaurou o contexto dela antes do rte que a retomou).
// (declarada em sh4_runtime.h, usada por BS_RTE)

// Numero de threads do guest ja vistas (diagnostico e testes).
int sh4_thread_count();

} // namespace bs
