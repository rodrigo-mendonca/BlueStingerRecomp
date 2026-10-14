// bs-builder: o fluxo completo do Builder pela linha de comando (automacao e testes).
//   bs-builder --disc <imagem.cdi> --out <pasta> [--kit <pasta>] [--sdl3 <pasta>]
//              [--vcvars <vcvars64.bat> | --vswhere <vswhere.exe>] [--clean] [--verify-vblanks N]
// Codigo de saida: 0 = BlueStinger.exe instalado e verificado; 1 = falha; 2 = uso;
// 3 = cancelado (Ctrl+C).
#include "builder_core.h"
#include "child_process.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bs::builder;

namespace {

std::atomic<bool> g_cancel{false};

BOOL WINAPI onCtrl(DWORD) {
    g_cancel = true;
    return TRUE;
}

void out(const std::string& s) {  // UTF-8 no console e em pipes
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

fs::path exeDir() {
    std::wstring b(32768, L'\0');
    b.resize(GetModuleFileNameW(nullptr, b.data(), DWORD(b.size())));
    return fs::path(b).parent_path();
}

} // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    int     argc = 0;
    wchar_t** wv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(wv, wv + argc);
    LocalFree(wv);

    Request req;
    req.kitDir = exeDir() / L"kit";
    for (size_t i = 1; i < args.size(); ++i) {
        const std::wstring& a    = args[i];
        const bool          more = i + 1 < args.size();
        if (a == L"--disc" && more) req.disc = args[++i];
        else if (a == L"--out" && more) req.outDir = args[++i];
        else if (a == L"--kit" && more) req.kitDir = args[++i];
        else if (a == L"--sdl3" && more) req.sdl3Dir = args[++i];
        else if (a == L"--vcvars" && more) req.vcvars = args[++i];
        else if (a == L"--vswhere" && more) req.vswhere = args[++i];
        else if (a == L"--verify-vblanks" && more) req.verifyVblanks = unsigned(std::stoul(args[++i]));
        else if (a == L"--clean") req.clean = true;
        else {
            std::fprintf(stderr,
                         "uso: bs-builder --disc <imagem.cdi> --out <pasta> [--kit <pasta>] [--sdl3 <pasta>]\n"
                         "                [--vcvars <vcvars64.bat> | --vswhere <vswhere.exe>] [--clean]\n"
                         "                [--verify-vblanks N]\n");
            return 2;
        }
    }
    if (req.disc.empty() || req.outDir.empty()) {
        std::fprintf(stderr, "uso: bs-builder --disc <imagem.cdi> --out <pasta> (veja --help)\n");
        return 2;
    }
    SetConsoleCtrlHandler(onCtrl, TRUE);

    Callbacks cb;
    cb.cancel   = &g_cancel;
    int lastPct = -1;
    Step lastStep = Step::Done;
    cb.progress = [&](Step s, double f, const std::string&) {
        const int pct = int(f * 100);
        if (pct != lastPct || s != lastStep) {
            out("[progresso] " + std::to_string(pct) + "% " + step_name(s) + "\n");
            lastPct = pct, lastStep = s;
        }
    };
    const bool verbose = GetEnvironmentVariableW(L"BS_BUILDER_VERBOSE", nullptr, 0) > 0;
    cb.log = [&](const std::string& l) {
        if (verbose || l.rfind("== ", 0) == 0 || l.rfind("ERRO", 0) == 0 || l.rfind("OK:", 0) == 0) out(l + "\n");
    };
    const Result r = build(req, cb);
    if (r.ok) {
        out("instalado: " + path_text(r.exe) + "\n");
        return 0;
    }
    out(std::string("falhou em \"") + step_name(r.failedStep) + "\": " + r.error + "\n");
    out("log completo: " + path_text(r.logFile) + "\n");
    return r.cancelled ? 3 : 1;
}
