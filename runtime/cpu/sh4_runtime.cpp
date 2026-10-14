// Implementacao M3 do contrato de sh4_runtime.h: despacho real e tratamento
// explicito (log + abort) de tudo que ainda nao existe. M4 substitui os stubs
// de excecao/sleep/store queue pelo hardware de verdade.
#include "cpu/sh4_runtime.h"

#include "cpu/sh4_intc.h"
#include "cpu/sh4_mem.h"
#include "gdrom/gdrom.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace bs {
namespace {

const Sh4FuncEntry* g_table = nullptr;
size_t              g_count = 0;
Sh4FatalFn          g_fatal = nullptr;
std::unordered_map<uint32_t, Sh4Func> g_hle;

[[noreturn]] void fatal(Sh4Context* c, const char* what, uint32_t pc, uint32_t extra) {
    if (g_fatal) g_fatal(c, what, pc, extra);  // pode lancar
    std::fprintf(stderr, "[sh4] FATAL: %s em PC=%08X (valor %08X) PR=%08X R15=%08X\n", what, pc, extra,
                 c ? c->pr : 0, c ? c->r[15] : 0);
    std::fflush(stderr);
    std::abort();
}

} // namespace

Sh4TraceFn g_sh4_trace = nullptr;

void sh4_set_fatal_handler(Sh4FatalFn fn) { g_fatal = fn; }

void sh4_fatal(Sh4Context* c, const char* what, uint32_t pc, uint32_t extra) { fatal(c, what, pc, extra); }

void sh4_set_function_table(const Sh4FuncEntry* table, size_t count) {
    g_table = table;
    g_count = count;
}

Sh4Func sh4_lookup(uint32_t addr) {
    const uint32_t p  = sh4_phys(addr);
    const auto*    it = std::lower_bound(g_table, g_table + g_count, p,
                                         [](const Sh4FuncEntry& e, uint32_t v) { return e.addr < v; });
    if (it != g_table + g_count && it->addr == p) return it->fn;
    const auto h = g_hle.find(p);
    return h != g_hle.end() ? h->second : nullptr;
}

void sh4_register_hle(uint32_t addr, Sh4Func fn) { g_hle[sh4_phys(addr)] = fn; }

void sh4_dispatch(Sh4Context* c, uint32_t from_pc, uint32_t target) {
    if (Sh4Func f = sh4_lookup(target)) {
        f(c);
        return;
    }
    sh4_dispatch_miss(c, from_pc, target);
}

void sh4_dispatch_miss(Sh4Context* c, uint32_t from_pc, uint32_t target) {
    fatal(c, "desvio indireto para endereco sem funcao recompilada", from_pc, target);
}

void sh4_bad_return(Sh4Context* c, uint32_t pc, uint32_t target) {
    fatal(c, "retorno para endereco inesperado", pc, target);
}

void sh4_unreachable(Sh4Context* c, uint32_t pc) { fatal(c, "fluxo chegou em dado/codigo nao analisado", pc, 0); }

void sh4_illegal(Sh4Context* c, uint32_t pc, uint16_t opcode) { fatal(c, "instrucao ilegal", pc, opcode); }

void sh4_trapa(Sh4Context* c, uint32_t imm) { fatal(c, "trapa (ainda nao implementado)", c->pc, imm); }

void sh4_address_error(Sh4Context* c, uint32_t addr, bool write) {
    fatal(c, write ? "address error (escrita desalinhada)" : "address error (leitura desalinhada)", c->pc, addr);
}

void sh4_sleep(Sh4Context* c) { sh4_service(c); }

void sh4_ldtlb(Sh4Context* c) { fatal(c, "ldtlb (MMU nao suportada)", c->pc, 0); }


void sh4_check_code(Sh4Context* c, const uint32_t* pcs, const uint16_t* ops, size_t n) {
    for (size_t k = 0; k < n; ++k)
        if (sh4_read16(c, pcs[k]) != ops[k]) fatal(c, "codigo em RAM difere do compilado (relocated)", pcs[k], ops[k]);
}

// Poll periodico (BS_POLL nos back-edges): reavalia interrupcoes a cada quantum de
// ciclos. c->pc ja e o endereco de retomada.
void sh4_service(Sh4Context* c) {
    c->next_event = c->cycles + kServiceQuantum;
    // transferencia do GD-ROM em andamento (perfil de tempo reios): avanca com o tempo,
    // sem depender de nova syscall (revisao Codex 2026-10-14)
    gdrom_poll(c);
    sh4_irq_check(c);
}

} // namespace bs
