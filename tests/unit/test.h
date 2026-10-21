#pragma once
// Mini-framework de testes sem dependencias (pode virar doctest no M2).
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <utility>
#include <vector>

namespace bs::test {

struct Case {
    const char*           name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& skips() { static int s = 0; return s; }  // casos que nao conferiram nada (dados do jogo ausentes)

struct Reg {
    Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

} // namespace bs::test

#define BS_TEST(name)                                         \
    static void name();                                       \
    static ::bs::test::Reg reg_##name(#name, name);           \
    static void name()

// Caso que depende de dados ausentes: aparece como [skip], nao como [ ok ]. Com BS_EXIGIR_DADOS=1 vira falha (a
// validacao de fidelidade exige os dados do disco; revisao Codex 2026-10-20).
#define BS_SKIP(motivo)                                                                             \
    do {                                                                                            \
        if (const char* exigir_ = std::getenv("BS_EXIGIR_DADOS"); exigir_ && *exigir_ == '1') {    \
            std::fprintf(stderr, "  FALHA %s:%d: dados exigidos: %s\n", __FILE__, __LINE__, motivo); \
            ++::bs::test::failures();                                                               \
        } else {                                                                                    \
            ++::bs::test::skips();                                                                  \
        }                                                                                           \
        return;                                                                                     \
    } while (0)

#define BS_CHECK(cond)                                                              \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "  FALHA %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++::bs::test::failures();                                               \
        }                                                                           \
    } while (0)
