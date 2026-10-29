#include "test.h"

#include <cstdlib>
#include <cstring>

int main() {
    // BS_TEST_FILTRO=<trecho>: so os casos com o trecho no nome (os negativos por mutacao rodam um caso isolado)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* filter = std::getenv("BS_TEST_FILTRO");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    for (auto& c : bs::test::registry()) {
        if (filter && *filter && !std::strstr(c.name, filter)) continue;
        const int before = bs::test::failures(), skipsBefore = bs::test::skips();
        c.fn();
        const char* r = bs::test::failures() != before ? "FAIL" : bs::test::skips() != skipsBefore ? "skip" : " ok ";
        std::printf("[%s] %s\n", r, c.name);
        std::fflush(stdout);  // saida em arquivo: o ultimo teste fica visivel se outro travar
    }
    // contagem no fim: uma saida cortada (processo interrompido) nao passa por aprovada (revisao Codex 2026-10-20);
    // os casos sem dados do jogo aparecem a parte, nunca somados aos que conferiram (revisao Codex 2026-10-20, BAIXA)
    std::printf("%zu testes, %d falhas, %d sem dados (skip)\n", bs::test::registry().size(), bs::test::failures(),
                bs::test::skips());
    return bs::test::failures() ? 1 : 0;
}
