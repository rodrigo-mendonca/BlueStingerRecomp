#include "cpu/sh4_threads.h"

#include "cpu/sh4_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace bs {
namespace {

struct Thread {
    void*    fiber = nullptr;
    uint32_t resumePc = 0, resumeSp = 0;  // onde a thread espera ser retomada
    bool     suspended = false;
    // estado C da cadeia de chamadas desta thread (salvo ao suspender)
    uint32_t retExpect = 0, retSp = 0;
    int      depth = 0;  // continuacoes aninhadas na pilha desta fibra
    uint32_t entry = 0;  // thread nova: primeira instrucao
};

std::vector<Thread> g_threads;  // [0] = thread em que o jogo comecou
size_t              g_current = 0;
Sh4Context*         g_ctx     = nullptr;
// Excecao (erro fatal do bs-diff, fim de trace) lancada numa fibra: nao pode atravessar
// para outra pilha; volta para a thread 0 e e relancada la.
std::exception_ptr  g_pending;

bool debugOn() {
    static const bool on = std::getenv("BS_IRQ_DEBUG") != nullptr;
    return on;
}

void ensureMain() {
    if (!g_threads.empty()) return;
    Thread t;
    // FLOAT_SWITCH: XMM6-XMM15 sao preservados entre chamadas na ABI x64 e podem estar
    // vivos numa fibra suspensa (revisao Codex 2026-10-14)
    t.fiber = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    if (!t.fiber) t.fiber = GetCurrentFiber();  // a thread do host ja era fibra
    g_threads.push_back(t);
}

// Suspende a thread atual (retomada em resumePc/resumeSp) e passa a `next`.
void switchTo(Sh4Context* c, size_t next) {
    Thread& cur   = g_threads[g_current];
    cur.suspended = true;
    cur.resumePc  = c->ret_expect;
    cur.resumeSp  = c->ret_sp;
    cur.retExpect = c->ret_expect;
    cur.retSp     = c->ret_sp;
    const size_t self = g_current;
    g_current         = next;
    Thread& n         = g_threads[next];
    n.suspended       = false;
    c->ret_expect     = n.retExpect;
    c->ret_sp         = n.retSp;
    SwitchToFiber(n.fiber);
    // retomada: alguem fez rte para resumePc/resumeSp desta thread (ou uma fibra que
    // terminou com erro devolveu o controle a thread 0)
    g_current                 = self;
    g_threads[self].suspended = false;
    // FLOAT_SWITCH restaura o MXCSR antigo desta fibra: o modo de FP segue o FPSCR (unico)
    sh4_apply_host_fp_mode(c->fpscr);
    c->ret_expect = g_threads[self].retExpect;
    c->ret_sp     = g_threads[self].retSp;
    if (g_pending) std::rethrow_exception(std::exchange(g_pending, nullptr));
}

void WINAPI fiberMain(void* arg) {
    const size_t self = size_t(reinterpret_cast<uintptr_t>(arg));
    Sh4Context*  c    = g_ctx;
    sh4_apply_host_fp_mode(c->fpscr);
    try {
        sh4_dispatch(c, 0, g_threads[self].entry);
        // uma thread do guest nao retorna da funcao de entrada (o escalonador troca antes)
        sh4_fatal(c, "funcao de entrada de thread do guest retornou", c->pc, g_threads[self].entry);
    } catch (...) {
        g_pending = std::current_exception();
    }
    g_current = 0;
    SwitchToFiber(g_threads[0].fiber);  // a thread 0 relanca; esta fibra nao volta
}

} // namespace

int sh4_thread_count() { return int(g_threads.size()); }

void sh4_rte_redirect(Sh4Context* c, uint32_t pc, uint32_t target) {
    ensureMain();
    g_ctx = c;
    const uint32_t sp = c->r[15];
    if (debugOn())
        std::fprintf(stderr, "[rte] em %08X para %08X r15=%08X (esperado %08X r15=%08X) thread=%zu\n", pc, target, sp,
                     c->ret_expect, c->ret_sp, g_current);
    for (size_t i = 0; i < g_threads.size(); ++i) {
        const Thread& t = g_threads[i];
        if (i != g_current && t.suspended && t.resumePc == target && t.resumeSp == sp) {
            switchTo(c, i);
            return;
        }
    }
    if (sp == c->ret_sp) {  // mesma thread: continuacao (rotina adiada da interrupcao)
        Thread& cur = g_threads[g_current];
        if (cur.depth >= 64) sh4_fatal(c, "continuacoes de rte aninhadas demais", pc, target);
        ++cur.depth;
        sh4_dispatch(c, pc, target);
        --g_threads[g_current].depth;
        return;
    }
    // thread nova
    Thread t;
    t.entry     = target;
    t.retExpect = 0xFFFFFFFF;  // a funcao de entrada nunca retorna
    t.retSp     = sp;
    const size_t id = g_threads.size();
    t.fiber = CreateFiberEx(64 << 10, 8u << 20, FIBER_FLAG_FLOAT_SWITCH, fiberMain,
                            reinterpret_cast<void*>(uintptr_t(id)));
    if (!t.fiber) sh4_fatal(c, "nao foi possivel criar fibra para thread do guest", pc, target);
    g_threads.push_back(t);
    if (debugOn()) std::fprintf(stderr, "[thread] nova thread %zu em %08X r15=%08X\n", id, target, sp);
    switchTo(c, id);
}

} // namespace bs
