// Prova de tests/sem_dialogos.cpp (teste ctest "sem_dialogos"). O processo roda a si mesmo como filho ("filho"):
// no Debug o filho indexa fora do vetor e dispara o assert do CRT que antes abria a janela modal; no Release (sem
// esse assert), abort(). O pai so passa se o filho terminar com erro; o ctest exige a mensagem na saida e um limite
// curto: se a janela voltasse, o filho ficaria parado e o teste estouraria o tempo.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <process.h>
#include <vector>

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "filho") == 0) {
        std::fprintf(stderr, "sem_dialogos: filho provocando a falha\n");
        std::fflush(stderr);
#ifdef _DEBUG
        std::vector<int> v(argc - 2);  // vazio
        return v[3];
#else
        std::fprintf(stderr, "sem_dialogos: abort (Release)\n");
        std::abort();
#endif
    }
    const intptr_t rc = _spawnl(_P_WAIT, argv[0], argv[0], "filho", nullptr);
    std::printf("sem_dialogos: filho terminou com 0x%08X\n", static_cast<unsigned>(rc));
    return rc != 0 ? 0 : 1;
}
