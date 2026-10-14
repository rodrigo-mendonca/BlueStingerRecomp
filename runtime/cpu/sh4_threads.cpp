#include "cpu/sh4_threads.h"

#include "cpu/sh4_runtime.h"

#include <algorithm>
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
    uint32_t redirect = 0;  // retomada por longjmp: ponto de retorno do setjmp
};

std::vector<Thread> g_threads;  // [0] = thread em que o jogo comecou
size_t              g_current = 0;
Sh4Context*         g_ctx     = nullptr;
// Excecao (erro fatal do bs-diff, fim de trace) lancada numa fibra: nao pode atravessar
// para outra pilha; volta para a thread 0 e e relancada la.
std::exception_ptr  g_pending;
std::vector<uint32_t> g_reentry;  // pontos de reentrada (retornos de setjmp), ordenados

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
    // retomada por longjmp: os quadros sobem ate o que chamou o setjmp (BS_CALL_REENTRANT)
    c->redirect = std::exchange(g_threads[self].redirect, 0u);
    if (g_pending) std::rethrow_exception(std::exchange(g_pending, nullptr));
}

void WINAPI fiberMain(void* arg) {
    const size_t self = size_t(reinterpret_cast<uintptr_t>(arg));
    Sh4Context*  c    = g_ctx;
    sh4_apply_host_fp_mode(c->fpscr);
    try {
        sh4_dispatch(c, 0, g_threads[self].entry);
        // longjmp que nenhum quadro desta thread reconheceu (subiu ate a entrada)
        if (c->redirect) sh4_fatal(c, "longjmp sem quadro de destino nesta thread", c->pc, c->redirect);
        // uma thread do guest nao retorna da funcao de entrada (o escalonador troca antes)
        sh4_fatal(c, "funcao de entrada de thread do guest retornou", c->pc, g_threads[self].entry);
    } catch (...) {
        g_pending = std::current_exception();
    }
    g_current = 0;
    SwitchToFiber(g_threads[0].fiber);  // a thread 0 relanca; esta fibra nao volta
}


// Thread nova numa fibra nova que comeca em `target`, com a pilha do guest em R15.
void startThread(Sh4Context* c, uint32_t pc, uint32_t target) {
    const uint32_t sp = c->r[15];
    Thread         t;
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
    startThread(c, pc, target);  // thread nova
}

// longjmp (biblioteca de threads cooperativa do jogo): o guest ja restaurou o contexto
// salvo pelo setjmp (registradores, R15, PR) e fez rts para o ponto de retorno dele.
//  - `target` e ponto de reentrada (retorno de setjmp, tabela do gerador): retoma a
//    unica thread suspensa cuja chamada pendente foi feita com esse R15 (o setjmp e a
//    chamada que a suspendeu estao no mesmo quadro); a fibra sobe ate esse quadro e
//    salta para `target` (BS_CALL_REENTRANT). Nenhuma ou mais de uma: erro fatal;
//  - `target` e entrada de funcao (e nao reentrada) numa pilha que nao e a desta
//    thread: thread nova (contexto montado pelo jogo, com PR = entrada e pilha nova);
//  - o resto (inclusive longjmp para quadro da propria thread) e erro fatal.
void sh4_longjmp_redirect(Sh4Context* c, uint32_t pc, uint32_t target) {
    ensureMain();
    g_ctx = c;
    const uint32_t sp = c->r[15];
    if (debugOn())
        std::fprintf(stderr, "[longjmp] em %08X para %08X r15=%08X thread=%zu\n", pc, target, sp, g_current);
    if (std::binary_search(g_reentry.begin(), g_reentry.end(), target)) {
        size_t found = g_threads.size(), n = 0;
        for (size_t i = 0; i < g_threads.size(); ++i)
            if (i != g_current && g_threads[i].suspended && g_threads[i].resumeSp == sp) found = i, ++n;
        if (n == 0) sh4_fatal(c, "longjmp para retorno de setjmp sem thread suspensa com esse R15", pc, target);
        if (n > 1) sh4_fatal(c, "longjmp ambiguo: mais de uma thread suspensa com esse R15", pc, target);
        g_threads[found].redirect = target;
        switchTo(c, found);
        return;
    }
    if (sp == c->ret_sp) sh4_fatal(c, "longjmp para quadro da propria thread (sem suporte)", pc, target);
    if (!sh4_lookup(target)) sh4_fatal(c, "longjmp para endereco que nao e retomada nem entrada de funcao", pc, target);
    startThread(c, pc, target);
}

void sh4_set_reentry_points(const uint32_t* points, size_t count) {
    g_reentry.assign(points, points + count);
    std::sort(g_reentry.begin(), g_reentry.end());
}

} // namespace bs
