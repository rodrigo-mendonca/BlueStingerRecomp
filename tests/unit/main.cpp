#include "test.h"

int main() {
    for (auto& c : bs::test::registry()) {
        int before = bs::test::failures();
        c.fn();
        std::printf("[%s] %s\n", bs::test::failures() == before ? " ok " : "FAIL", c.name);
        std::fflush(stdout);  // saida em arquivo: o ultimo teste fica visivel se outro travar
    }
    // contagem no fim: uma saida cortada (processo interrompido) nao passa por aprovada (revisao Codex 2026-10-20)
    std::printf("%zu testes, %d falhas\n", bs::test::registry().size(), bs::test::failures());
    return bs::test::failures() ? 1 : 0;
}
