// longjmp entre threads do guest (biblioteca cooperativa do Blue Stinger, ver
// config/bluestinger_us.toml [threads]): runtime (sh4_longjmp_redirect + fibras) com
// funcoes escritas como o codigo gerado (mesmas macros e o mesmo despacho de
// reentrada), e o gerador (pontos de reentrada).
#include "test.h"

#include "analysis/analysis.h"
#include "codegen/codegen.h"
#include "cpu/sh4_gen.h"
#include "cpu/sh4_threads.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bs;

namespace {

constexpr uint32_t kSetjmp = 0x8C003000, kLongjmp = 0x8C003100;
constexpr uint32_t kSched = 0x8C003300, kWorker = 0x8C003200;          // cenario 1
constexpr uint32_t kOuter = 0x8C003400, kInner = 0x8C003500, kW2 = 0x8C003600;  // cenario 2
// retornos de setjmp (pontos de reentrada)
constexpr uint32_t kSchedRet = 0x8C003304, kWorkerRet = 0x8C003204, kOuterRet = 0x8C003404, kInnerRet = 0x8C003504;
// pilhas do guest (unicas nos testes)
constexpr uint32_t kSpS = 0x8C2A0000, kSpW = 0x8C290000, kSpO = 0x8C270000, kSpW2 = 0x8C260000;

struct Ctx {
    uint32_t r15 = 0, pr = 0;
};
Ctx g_ctx[4];  // no jogo, memoria do guest

struct {
    int      schedResumes = 0, workerRuns = 0, workerResumes = 0, outerResumes = 0, innerRuns = 0;
    uint32_t r0OnResume[4] = {}, outerR0 = 0;
} g_lj;

// setjmp(r4 = contexto): salva R15 e PR, devolve 0
void fSetjmp(Sh4Context* c) {
    BS_FN_ENTER(kSetjmp);
    g_ctx[c->r[4]] = {c->r[15], c->pr};
    c->r[0]        = 0;
    { const uint32_t tgt = c->pr; BS_RET(kSetjmp + 2, tgt); }
}

// longjmp(r4 = contexto, r5 = valor): restaura R15 e PR, r0 = valor (1 se 0), rts
void fLongjmp(Sh4Context* c) {
    BS_FN_ENTER(kLongjmp);
    const Ctx& x = g_ctx[c->r[4]];
    c->r[0]      = c->r[5] ? c->r[5] : 1;
    c->r[15]     = x.r15;
    c->pr        = x.pr;
    { const uint32_t tgt = c->pr; BS_LONGJMP_RET(kLongjmp + 2, tgt); }
}

// --- cenario 1: escalonador + worker (yield) ---

// Thread nova: setjmp(1); se 0, yield (longjmp(0, 1)); retomada: longjmp(0, 2).
void fWorker(Sh4Context* c) {
    BS_FN_ENTER(kWorker);
    ++g_lj.workerRuns;
    c->r[4] = 1; c->pr = kWorkerRet;
    BS_CALL_REENTRANT(fSetjmp(c), kWorkerRet);
L_ret:;
    if (c->r[0] == 0) {
        c->r[4] = 0; c->r[5] = 1; c->pr = 0x8C003210;
        BS_CALL_REENTRANT(fLongjmp(c), 0x8C003210);
        ::bs::sh4_unreachable(c, 0x8C003210); return;
    }
    ++g_lj.workerResumes;
    c->r[4] = 0; c->r[5] = 2; c->pr = 0x8C003220;
    BS_CALL_REENTRANT(fLongjmp(c), 0x8C003220);
    ::bs::sh4_unreachable(c, 0x8C003220); return;
bs_reenter_:;
    switch (c->redirect) {
    case kWorkerRet: c->redirect = 0; goto L_ret;
    default: return;
    }
}

// Escalonador (thread 0): setjmp(0); se 0, inicia o worker (contexto com PR = entrada e
// pilha nova); r0 = 1 (yield): retoma o worker; r0 = 2: fim.
void fSched(Sh4Context* c) {
    BS_FN_ENTER(kSched);
    c->r[4] = 0; c->pr = kSchedRet;
    BS_CALL_REENTRANT(fSetjmp(c), kSchedRet);
L_ret:;
    if (c->r[0] == 0) {
        g_ctx[1] = {kSpW, kWorker};
        c->r[4] = 1; c->r[5] = 1; c->pr = 0x8C003310;
        BS_CALL_REENTRANT(fLongjmp(c), 0x8C003310);
        ::bs::sh4_unreachable(c, 0x8C003310); return;
    }
    g_lj.r0OnResume[g_lj.schedResumes++ & 3] = c->r[0];
    if (c->r[0] == 1) {
        c->r[4] = 1; c->r[5] = 1; c->pr = 0x8C003320;
        BS_CALL_REENTRANT(fLongjmp(c), 0x8C003320);
        ::bs::sh4_unreachable(c, 0x8C003320); return;
    }
    return;
bs_reenter_:;
    switch (c->redirect) {
    case kSchedRet: c->redirect = 0; goto L_ret;
    default: return;
    }
}

// --- cenario 2: dois quadros reentrantes com o mesmo R15 (revisao Codex 2026-10-14) ---

// Interno: chamado por fOuter sem empilhar nada (mesmo R15); tem setjmp proprio
// (contexto 2) e inicia W2, que faz longjmp para o contexto do EXTERNO.
void fInner(Sh4Context* c) {
    BS_FN_ENTER(kInner);
    ++g_lj.innerRuns;
    c->r[4] = 2; c->pr = kInnerRet;
    BS_CALL_REENTRANT(fSetjmp(c), kInnerRet);
L_ret:;
    g_ctx[1] = {kSpW2, kW2};
    c->r[4] = 1; c->r[5] = 1; c->pr = 0x8C003510;
    BS_CALL_REENTRANT(fLongjmp(c), 0x8C003510);
    ::bs::sh4_unreachable(c, 0x8C003510); return;
bs_reenter_:;
    switch (c->redirect) {
    case kInnerRet: c->redirect = 0; goto L_ret;
    default: return;
    }
}

void fW2(Sh4Context* c) {  // longjmp(contexto do externo, 3)
    BS_FN_ENTER(kW2);
    c->r[4] = 0; c->r[5] = 3; c->pr = 0x8C003610;
    BS_CALL(fLongjmp(c), 0x8C003610);
    ::bs::sh4_unreachable(c, 0x8C003610);
}

void fOuter(Sh4Context* c) {
    BS_FN_ENTER(kOuter);
    c->r[4] = 0; c->pr = kOuterRet;
    BS_CALL_REENTRANT(fSetjmp(c), kOuterRet);
L_ret:;
    if (c->r[0] == 0) {
        c->pr = 0x8C003410;
        BS_CALL_REENTRANT(fInner(c), 0x8C003410);
        ::bs::sh4_unreachable(c, 0x8C003410); return;
    }
    ++g_lj.outerResumes;
    g_lj.outerR0 = c->r[0];
    return;
bs_reenter_:;
    switch (c->redirect) {
    case kOuterRet: c->redirect = 0; goto L_ret;
    default: return;
    }
}

const Sh4FuncEntry kTable[] = {{kSetjmp & 0x1FFFFFFF, fSetjmp}, {kLongjmp & 0x1FFFFFFF, fLongjmp},
                               {kWorker & 0x1FFFFFFF, fWorker}, {kSched & 0x1FFFFFFF, fSched},
                               {kOuter & 0x1FFFFFFF, fOuter},   {kInner & 0x1FFFFFFF, fInner},
                               {kW2 & 0x1FFFFFFF, fW2}};
const uint32_t kReentry[] = {kWorkerRet, kSchedRet, kOuterRet, kInnerRet};

[[noreturn]] void throwingFatal(Sh4Context*, const char* what, uint32_t, uint32_t) { throw std::runtime_error(what); }

struct LjFixture {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    LjFixture() {
        c.ram        = ram.data();
        c.next_event = std::numeric_limits<uint64_t>::max();  // sem poll de eventos
        sh4_set_function_table(kTable, sizeof(kTable) / sizeof(kTable[0]));
        sh4_set_reentry_points(kReentry, 4);
        g_lj = {};
    }
    ~LjFixture() { sh4_set_reentry_points(nullptr, 0); }
};

// fatal esperado: devolve a mensagem
template <class F> std::string expectFatal(F&& fn) {
    sh4_set_fatal_handler(throwingFatal);
    std::string msg;
    try {
        fn();
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    sh4_set_fatal_handler(nullptr);
    return msg;
}

} // namespace

// Escalonador -> thread nova (fibra nova) -> yield de volta ao retorno do setjmp do
// escalonador (r0 = 1) -> retomada do worker no retorno do setjmp dele -> fim (r0 = 2).
BS_TEST(longjmp_switches_between_scheduler_and_worker_fibers) {
    LjFixture f;
    const int before = sh4_thread_count();
    f.c.r[15]        = kSpS;
    f.c.ret_expect   = 0x8C000500;
    f.c.ret_sp       = 0x8C2F0000;
    f.c.pr           = 0x8C000500;
    Sh4Context* c    = &f.c;  // as macros usam `c`
    BS_CALL(fSched(c), 0x8C000500);
    BS_CHECK(g_lj.workerRuns == 1 && g_lj.workerResumes == 1);
    BS_CHECK(g_lj.schedResumes == 2 && g_lj.r0OnResume[0] == 1 && g_lj.r0OnResume[1] == 2);
    BS_CHECK(sh4_thread_count() >= before + 1);
    BS_CHECK(f.c.redirect == 0 && f.c.r[15] == kSpS && f.c.ret_expect == 0x8C000500);
}

// O quadro interno tem o mesmo R15, mas o alvo e do externo: o interno deixa o
// redirect pendente e retorna; o externo o reconhece e continua com r0 = 3.
BS_TEST(longjmp_skips_inner_reentrant_frame_with_same_sp) {
    LjFixture f;
    f.c.r[15]      = kSpO;
    f.c.ret_expect = 0x8C000700;
    f.c.ret_sp     = 0x8C2F0000;
    Sh4Context* c  = &f.c;
    BS_CALL(fOuter(c), 0x8C000700);
    BS_CHECK(g_lj.innerRuns == 1 && g_lj.outerResumes == 1 && g_lj.outerR0 == 3);
    BS_CHECK(f.c.redirect == 0 && f.c.r[15] == kSpO);
}

// Alvo que nao e retomada de thread suspensa nem entrada de funcao: fatal.
BS_TEST(longjmp_to_unknown_target_is_fatal) {
    LjFixture f;
    const std::string msg = expectFatal([&] {
        g_ctx[0]       = {0x8C280000, 0x8C00DEAA};
        f.c.r[4]       = 0;
        f.c.r[5]       = 1;
        f.c.r[15]      = 0x8C2E0000;
        f.c.ret_sp     = 0x8C2E0000;
        f.c.ret_expect = 0x8C000600;
        fLongjmp(&f.c);
    });
    BS_CHECK(msg.find("nao e retomada nem entrada") != std::string::npos);
}

// Ponto de reentrada sem thread suspensa com o R15 restaurado: fatal (nao cria thread).
BS_TEST(longjmp_to_reentry_without_suspended_thread_is_fatal) {
    LjFixture f;
    const int         before = sh4_thread_count();
    const std::string msg    = expectFatal([&] {
        g_ctx[0]       = {0x8C250000, kSchedRet};
        f.c.r[4]       = 0;
        f.c.r[5]       = 1;
        f.c.r[15]      = 0x8C2E0000;
        f.c.ret_sp     = 0x8C2E0000;
        f.c.ret_expect = 0x8C000600;
        fLongjmp(&f.c);
    });
    BS_CHECK(msg.find("sem thread suspensa") != std::string::npos);
    BS_CHECK(sh4_thread_count() == before);
}

// Gerador: o retorno de uma chamada a setjmp vira rotulo de reentrada; as chamadas da
// funcao usam BS_CALL_REENTRANT; alvo de outra funcao fica pendente (return); o rts de
// uma funcao longjmp usa BS_LONGJMP_RET.
BS_TEST(codegen_setjmp_return_is_reentry_label) {
    analysis::Image img;
    img.base = 0x8C010000;
    // 00: bsr 10 (setjmp); 02: nop; 04: tst r0,r0; 06: rts; 08: nop
    // 10: rts; 12: nop (setjmp)   14: rts; 16: nop (longjmp)
    for (int w : {0xB006, 0x0009, 0x2008, 0x000B, 0x0009, 0x0009, 0x0009, 0x0009, 0x000B, 0x0009, 0x000B, 0x0009}) {
        img.bytes.push_back(uint8_t(w));
        img.bytes.push_back(uint8_t(w >> 8));
    }
    analysis::Config cfg;
    cfg.entry = 0x8C010000;
    cfg.extra_functions.push_back(0x8C010014);
    const auto       a = analysis::analyze(img, cfg);
    codegen::Options opt;
    opt.setjmp_funcs  = {0x8C010010};
    opt.longjmp_funcs = {0x8C010014};
    const std::string m = codegen::emitFunction(img, a, a.functions.at(0x8C010000), opt);
    BS_CHECK(m.find("BS_CALL_REENTRANT(BSG_FN(8C010010)(c), 0x8C010004u)") != std::string::npos);
    BS_CHECK(m.find("L_8C010004:;") != std::string::npos);
    BS_CHECK(m.find("case 0x8C010004u: c->redirect = 0; goto L_8C010004;") != std::string::npos);
    BS_CHECK(m.find("default: return;") != std::string::npos);
    BS_CHECK(m.find("bs_reenter_:;") != std::string::npos);
    const std::string lj = codegen::emitFunction(img, a, a.functions.at(0x8C010014), opt);
    BS_CHECK(lj.find("BS_LONGJMP_RET(0x8C010014u, tgt)") != std::string::npos);
    // sem [threads], nada muda
    const std::string plain = codegen::emitFunction(img, a, a.functions.at(0x8C010000), codegen::Options{});
    BS_CHECK(plain.find("REENTRANT") == std::string::npos && plain.find("bs_reenter_") == std::string::npos);
}
