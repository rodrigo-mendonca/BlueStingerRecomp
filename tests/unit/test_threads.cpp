// Threads do guest sobre fibras (runtime/cpu/sh4_threads.cpp), com funcoes falsas do
// guest no despacho. Cenario do boot do Blue Stinger: a thread principal chama uma
// funcao de troca que termina em `rte` para outra thread (outra pilha), e o escalonador
// volta com `rte` para o retorno daquela chamada.
#include "test.h"

#include "cpu/sh4_context.h"
#include "cpu/sh4_runtime.h"
#include "cpu/sh4_threads.h"

#include <cfenv>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace bs;

extern "C" void xmm_probe(const double* in, double* out, void (*cb)(void*), void* arg);

namespace {

constexpr uint32_t kSpA = 0x8C00F300, kSpB = 0x8C2D5D00, kSpE = 0x8C2C0000;
constexpr uint32_t kRetA1 = 0x8C000100, kRetA2 = 0x8C000200, kRetB = 0x8C001200;
constexpr uint32_t kEntryB = 0x8C001100, kEntryC = 0x8C001300, kEntryE = 0x8C001500;

struct Log {
    int       bRuns = 0, bResumed = 0, cRuns = 0;
    uintptr_t stackA = 0, stackB = 0;
    double    junk = 0;
} g_log;

// Thread B: comeca em kEntryB; muda o arredondamento; "chama" a funcao de troca
// (ret_expect = kRetB, ret_sp = kSpB) que faz rte para a thread A.
void threadB(Sh4Context* c) {
    int local = 0;
    g_log.stackB = reinterpret_cast<uintptr_t>(&local);
    ++g_log.bRuns;
    sh4_set_fpscr(*c, (c->fpscr & ~3u) | 1u);  // RM = 01: para zero
    volatile double junk = 0;  // contas de ponto flutuante nesta fibra (sujam os XMM)
    for (int i = 1; i < 64; ++i) junk = junk * 1.0001 + 1.0 / i;
    g_log.junk = junk;
    c->ret_expect = kRetB;
    c->ret_sp     = kSpB;
    c->r[15]      = kSpA;  // o guest restaurou o contexto de A
    sh4_rte_redirect(c, kEntryB, kRetA1);
    // retomada de B
    ++g_log.bResumed;
    c->r[15] = kSpA;
    sh4_rte_redirect(c, kEntryB, kRetA2);
}

// Continuacao na mesma thread (rotina adiada do tratador de interrupcao).
void contC(Sh4Context*) { ++g_log.cRuns; }

[[noreturn]] void threadE(Sh4Context* c) { sh4_fatal(c, "teste: erro na thread E", kEntryE, 0); }

[[noreturn]] void throwingFatal(Sh4Context*, const char* what, uint32_t, uint32_t) { throw std::runtime_error(what); }

// Preservacao de XMM6-XMM15: xmm_probe (tests/unit/xmm_probe.asm) carrega sentinelas,
// chama o callback (que troca de fibra) e devolve o que encontrou nos registradores.
constexpr uint32_t kEntryX = 0x8C001700, kSpX = 0x8C2B0000, kRetAx = 0x8C000400, kRetX = 0x8C001800;
double g_inA[10], g_outA[10], g_inX[10], g_outX[10];

void cbX(void* arg) {  // na thread X, com as sentinelas de X carregadas: volta para A
    auto* c = static_cast<Sh4Context*>(arg);
    sh4_set_fpscr(*c, (c->fpscr & ~3u) | 1u);  // X muda o arredondamento do guest (RZ)
    c->ret_expect = kRetX;
    c->ret_sp     = kSpX;
    c->r[15]      = kSpA;
    sh4_rte_redirect(c, kEntryX, kRetAx);
}

void threadX(Sh4Context* c) {
    xmm_probe(g_inX, g_outX, cbX, c);
    sh4_fatal(c, "teste: thread X retomada sem pedido", kEntryX, 0);
}

void cbA(void* arg) {  // na thread A, com as sentinelas de A carregadas: inicia X
    auto* c       = static_cast<Sh4Context*>(arg);
    c->ret_expect = kRetAx;
    c->ret_sp     = kSpA;
    c->r[15]      = kSpX;
    sh4_rte_redirect(c, 0x8C000F40, kEntryX);
}

const Sh4FuncEntry kTable[] = {{kEntryB & 0x1FFFFFFF, threadB},
                               {kEntryC & 0x1FFFFFFF, contC},
                               {kEntryE & 0x1FFFFFFF, threadE},
                               {kEntryX & 0x1FFFFFFF, threadX}};

} // namespace

BS_TEST(threads_switch_resume_stacks_fpu_and_continuation) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    sh4_set_function_table(kTable, 4);
    g_log = {};
    sh4_set_fpscr(c, 0x00040001 & ~3u);  // RM = 00: ao mais proximo
    const int before = sh4_thread_count();

    int local = 0;
    g_log.stackA = reinterpret_cast<uintptr_t>(&local);
    // Valores de ponto flutuante vivos atravessando a troca (XMM6-XMM15 sao preservados
    // na ABI x64; com otimizacao o compilador os mantem em registradores, em debug na pilha)
    const double v1 = 1.5 + double(c.cycles), v2 = v1 * 3.25, v3 = v2 - 0.125;
    // A chama a funcao de troca: retorno esperado kRetA1, pilha kSpA; o rte vai para B
    c.ret_expect = kRetA1;
    c.ret_sp     = kSpA;
    c.r[15]      = kSpB;
    sh4_rte_redirect(&c, 0x8C000F00, kEntryB);
    // A retomada: B rodou numa fibra nova, com outra pilha do host
    BS_CHECK(g_log.bRuns == 1 && g_log.bResumed == 0);
    BS_CHECK(v1 == 1.5 && v2 == 4.875 && v3 == 4.75 && g_log.junk > 0);
    BS_CHECK(sh4_thread_count() >= before + 1);
    const uintptr_t dist = g_log.stackA > g_log.stackB ? g_log.stackA - g_log.stackB : g_log.stackB - g_log.stackA;
    BS_CHECK(dist > (64u << 10));
    BS_CHECK(c.ret_expect == kRetA1 && c.ret_sp == kSpA && c.r[15] == kSpA);
    // o modo de FP do host segue o FPSCR (unico), mesmo depois da troca de fibra
    BS_CHECK((c.fpscr & 3) == 1);
    BS_CHECK(std::fegetround() == FE_TOWARDZERO);

    // continuacao na mesma pilha: nao cria thread
    const int threads = sh4_thread_count();
    c.r[15] = kSpA;
    sh4_rte_redirect(&c, 0x8C000F10, kEntryC);
    BS_CHECK(g_log.cRuns == 1 && sh4_thread_count() == threads);

    // A volta para B (retorno da chamada de troca de B); B devolve para A em kRetA2
    c.ret_expect = kRetA2;
    c.ret_sp     = kSpA;
    c.r[15]      = kSpB;
    sh4_rte_redirect(&c, 0x8C000F20, kRetB);
    BS_CHECK(g_log.bResumed == 1 && c.ret_expect == kRetA2 && c.r[15] == kSpA);
    sh4_set_fpscr(c, 0x00040001 & ~3u);
}

// Erro fatal numa fibra nao atravessa a pilha: volta para a thread 0 e e relancado la.
BS_TEST(threads_fatal_error_in_fiber_reaches_main) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    sh4_set_function_table(kTable, 4);
    sh4_set_fatal_handler(throwingFatal);
    bool caught = false;
    try {
        c.ret_expect = 0x8C000300;
        c.ret_sp     = kSpA;
        c.r[15]      = kSpE;
        sh4_rte_redirect(&c, 0x8C000F30, kEntryE);
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()).find("thread E") != std::string::npos;
    }
    sh4_set_fatal_handler(nullptr);
    BS_CHECK(caught);
}

// XMM6-XMM15 (preservados na ABI x64) e o modo de arredondamento atravessam duas trocas
// de fibra. A e X carregam sentinelas diferentes; sem FIBER_FLAG_FLOAT_SWITCH, A veria
// as de X. Independe do nivel de otimizacao (os registradores sao usados em assembly).
BS_TEST(threads_preserve_xmm6_15_and_rounding_across_fibers) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    sh4_set_function_table(kTable, 4);
    sh4_set_fpscr(c, 0x00040000);  // RN
    for (int i = 0; i < 10; ++i) {
        g_inA[i]  = 1000.0 + i;
        g_inX[i]  = -2000.5 - i;
        g_outA[i] = g_outX[i] = 0;
    }
    xmm_probe(g_inA, g_outA, cbA, &c);
    bool same = true;
    for (int i = 0; i < 10; ++i) same = same && g_outA[i] == g_inA[i];
    BS_CHECK(same);
    BS_CHECK((c.fpscr & 3) == 1 && std::fegetround() == FE_TOWARDZERO);  // segue o FPSCR (de X)
    sh4_set_fpscr(c, 0x00040000);
}
