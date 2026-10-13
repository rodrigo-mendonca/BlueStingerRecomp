#pragma once
// Pontos de contato entre o codigo gerado e o runtime: despacho de chamadas
// indiretas, excecoes, cache/store queues e verificacoes de fluxo.
#include "cpu/sh4_context.h"

#include <cstddef>

namespace bs {

using Sh4Func = void (*)(Sh4Context*);

struct Sh4FuncEntry {
    uint32_t addr;  // endereco fisico (29 bits) da entrada da funcao
    Sh4Func  fn;
};

// Tabela de funcoes recompiladas (ordenada por addr). Registrada pelo executavel.
void          sh4_set_function_table(const Sh4FuncEntry* table, size_t count);
Sh4Func       sh4_lookup(uint32_t addr);  // nullptr se desconhecido
inline uint32_t sh4_phys(uint32_t addr) { return addr & 0x1FFFFFFF; }

// Chamada/salto indireto: procura `target` e executa. Endereco desconhecido vai
// para sh4_dispatch_miss (log + politica configurada), nunca em silencio.
void sh4_dispatch(Sh4Context* c, uint32_t from_pc, uint32_t target);
void sh4_dispatch_miss(Sh4Context* c, uint32_t from_pc, uint32_t target);

// rts/rte para um endereco diferente do que o chamador esperava.
void sh4_bad_return(Sh4Context* c, uint32_t pc, uint32_t target);
// O fluxo chegou num endereco que a analise classificou como dado/invalido.
void sh4_unreachable(Sh4Context* c, uint32_t pc);
// Instrucao sem traducao (p.ex. opcode invalido alcancado).
void sh4_illegal(Sh4Context* c, uint32_t pc, uint16_t opcode);

void sh4_trapa(Sh4Context* c, uint32_t imm);
void sh4_address_error(Sh4Context* c, uint32_t addr, bool write);  // word/longword desalinhado
void sh4_sleep(Sh4Context* c);
void sh4_ldtlb(Sh4Context* c);
void sh4_sq_flush(Sh4Context* c, uint32_t addr);  // pref em 0xE0000000-0xE3FFFFFF
void sh4_service(Sh4Context* c);                   // eventos/interrupcoes pendentes

// Rastreabilidade: toda funcao gerada chama BS_FN_ENTER(endereco SH-4). Com um
// observador registrado, cada entrada e reportada (testes diferenciais, traces).
using Sh4TraceFn = void (*)(Sh4Context* c, uint32_t entry);
// Erro fatal (despacho sem funcao, address error, ...). Sem tratador: log + abort.
// Um tratador (p.ex. bs-diff) pode lancar excecao para continuar o processo.
using Sh4FatalFn = void (*)(Sh4Context* c, const char* what, uint32_t pc, uint32_t extra);
void sh4_set_fatal_handler(Sh4FatalFn fn);
extern Sh4TraceFn g_sh4_trace;
inline void sh4_set_trace(Sh4TraceFn fn) { g_sh4_trace = fn; }

} // namespace bs
