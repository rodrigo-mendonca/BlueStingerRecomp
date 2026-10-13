#pragma once
// Mini-framework de testes sem dependencias (pode virar doctest no M2).
#include <cstdio>
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

struct Reg {
    Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

} // namespace bs::test

#define BS_TEST(name)                                         \
    static void name();                                       \
    static ::bs::test::Reg reg_##name(#name, name);           \
    static void name()

#define BS_CHECK(cond)                                                              \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "  FALHA %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++::bs::test::failures();                                               \
        }                                                                           \
    } while (0)
