// Executaveis de teste sem janelas modais: no Windows, um assert do CRT de Debug ("Debug Assertion
// Failed", p. ex. indice fora do vetor), um abort() ou uma falha de acesso abrem uma caixa de dialogo que
// espera um clique e trava o ctest/regressao indefinidamente (2026-10-20: 6,5 h parado num assert). Aqui
// a mensagem vai para o stderr e o processo termina com erro. Ligado a todos os testes em tests/CMakeLists.
#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>
#include <windows.h>

namespace {
struct SemDialogos {
    SemDialogos() {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        const int tipos[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
        for (int t : tipos) {
            _CrtSetReportMode(t, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
            _CrtSetReportFile(t, _CRTDBG_FILE_STDERR);
        }
    }
} g_semDialogos;
} // namespace
#endif
