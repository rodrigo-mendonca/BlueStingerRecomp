// Implementacao M3 do contrato de sh4_runtime.h: despacho real e tratamento
// explicito (log + abort) de tudo que ainda nao existe. M4 substitui os stubs
// de excecao/sleep/store queue pelo hardware de verdade.
#include "cpu/sh4_runtime.h"

#include "cpu/sh4_intc.h"
#include "cpu/time_hooks.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_threads.h"
#include "debug/hw_probe.h"
#include "gdrom/gdrom.h"
#include "pvr/holly_intc.h"

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

// Substituicoes feitas por mods (runtime/mods): valem antes da tabela gerada.
static std::unordered_map<uint32_t, Sh4Func> g_override;

void sh4_override(uint32_t addr, Sh4Func fn) {
    if (fn) g_override[sh4_phys(addr)] = fn;
    else g_override.erase(sh4_phys(addr));
}

Sh4Func sh4_lookup(uint32_t addr) {
    const uint32_t p  = sh4_phys(addr);
    if (!g_override.empty())
        if (const auto o = g_override.find(p); o != g_override.end()) return o->second;
    const auto*    it = std::lower_bound(g_table, g_table + g_count, p,
                                         [](const Sh4FuncEntry& e, uint32_t v) { return e.addr < v; });
    if (it != g_table + g_count && it->addr == p) return it->fn;
    const auto h = g_hle.find(p);
    return h != g_hle.end() ? h->second : nullptr;
}

Sh4Func sh4_lookup_generated(uint32_t addr) {
    const uint32_t p  = sh4_phys(addr);
    const auto*    it = std::lower_bound(g_table, g_table + g_count, p,
                                         [](const Sh4FuncEntry& e, uint32_t v) { return e.addr < v; });
    if (it != g_table + g_count && it->addr == p) return it->fn;
    const auto h = g_hle.find(p);
    return h != g_hle.end() ? h->second : nullptr;
}

void sh4_register_hle(uint32_t addr, Sh4Func fn) { g_hle[sh4_phys(addr)] = fn; }

void sh4_dispatch(Sh4Context* c, uint32_t from_pc, uint32_t target) {
    if (g_hw_probe) hw_probe_dispatch(c, target);
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
void sh4_wait_point(Sh4Context* c, uint32_t pc) {
    // N2b-2: se o instante logico andou, o poll que segue o ponto (BS_WAIT) atende os eventos
    // (next_event = 0 forca o atendimento, como no fim do DMA do canal 2)
    if (time_provider().wait(c, pc, uint32_t(sh4_current_thread()))) c->next_event = 0;
}

void sh4_wait_end(Sh4Context* c, uint32_t pc) {
    time_provider().wait_end(pc, uint32_t(sh4_current_thread()), c->polls);
}

void sh4_service(Sh4Context* c) {
    c->next_event = c->cycles + kServiceQuantum;
    time_provider().progress(uint32_t(sh4_current_thread()), c->polls, c->pc);
    // transferencia do GD-ROM em andamento (perfil de tempo reios): avanca com o tempo,
    // sem depender de nova syscall (revisao Codex 2026-10-14)
    gdrom_poll(c);
    // eventos do Holly/SPG (VBlank: video e ritmo do host) avancam com o tempo mesmo com
    // interrupcoes bloqueadas (SR.BL); a entrega para a CPU continua em sh4_irq_check
    holly_intc_sync(time_now(c));
    sh4_irq_check(c);
}

} // namespace bs
