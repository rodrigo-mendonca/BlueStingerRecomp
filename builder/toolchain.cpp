#include "toolchain.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <stdexcept>

namespace fs = std::filesystem;

namespace bs::builder {

const char* const kToolchainHelp =
    "O Builder compila o jogo com o compilador C++ da Microsoft, que nao vem junto.\n"
    "Instale o \"Build Tools para Visual Studio\" (gratuito) em\n"
    "https://visualstudio.microsoft.com/downloads/ (Ferramentas para Visual Studio)\n"
    "e marque a carga de trabalho \"Desenvolvimento para desktop com C++\"\n"
    "(inclui o MSVC x64, o SDK do Windows e as ferramentas CMake do C++).\n"
    "Um Visual Studio 2022 ou mais novo com essa carga de trabalho tambem serve.";

namespace {

std::wstring envGet(const Env& env, const wchar_t* name) {
    for (const auto& [k, v] : env)
        if (CompareStringOrdinal(k.c_str(), int(k.size()), name, -1, TRUE) == CSTR_EQUAL) return v;
    return {};
}

fs::path findOnPath(const Env& env, const wchar_t* exe) {
    const std::wstring path = envGet(env, L"PATH");
    size_t             a    = 0;
    while (a <= path.size()) {
        size_t b = path.find(L';', a);
        if (b == std::wstring::npos) b = path.size();
        if (b > a) {
            const fs::path p = fs::path(path.substr(a, b - a)) / exe;
            std::error_code ec;
            if (fs::is_regular_file(p, ec)) return p;
        }
        a = b + 1;
    }
    return {};
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\\' || s.back() == ' ')) s.pop_back();
    return s;
}

} // namespace

Toolchain find_toolchain(const ToolchainOptions& opt, const std::function<void(const std::string&)>& log) {
    Toolchain tc;
    auto      say = [&](const std::string& s) {
        if (log) log(s);
    };
    std::error_code ec;
    if (!opt.vcvars.empty()) {
        tc.vcvars = opt.vcvars;
        if (!fs::is_regular_file(tc.vcvars, ec))
            throw std::runtime_error("vcvars64.bat nao encontrado: " + path_text(tc.vcvars));
        tc.installRoot = tc.vcvars.parent_path().parent_path().parent_path().parent_path();
    } else {
        fs::path vswhere = opt.vswhere;
        if (vswhere.empty()) {
            const std::wstring pf = envGet(current_env(), L"ProgramFiles(x86)");
            vswhere = fs::path(pf.empty() ? L"C:\\Program Files (x86)" : pf) / L"Microsoft Visual Studio" /
                      L"Installer" / L"vswhere.exe";
        }
        if (!fs::is_regular_file(vswhere, ec))
            throw std::runtime_error(std::string("Visual Studio / Build Tools nao encontrado (sem ") +
                                     path_text(vswhere) + ").\n\n" + kToolchainHelp);
        std::string        found;
        const std::wstring cmd = quote_arg(vswhere.native()) +
                                 L" -latest -products * -prerelease -requires "
                                 L"Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -utf8";
        const auto r = run_process(cmd, {}, {}, [&](const std::string& l) {
            if (found.empty() && !l.empty()) found = l;
        });
        if (!r.started || r.exitCode != 0 || found.empty())
            throw std::runtime_error("nenhuma instalacao do Visual Studio com o compilador C++ x64 foi encontrada.\n\n" +
                                     std::string(kToolchainHelp));
        tc.installRoot = fs::path(widen(trim(found)));
        tc.vcvars      = tc.installRoot / L"VC" / L"Auxiliary" / L"Build" / L"vcvars64.bat";
        if (!fs::is_regular_file(tc.vcvars, ec))
            throw std::runtime_error("instalacao incompleta: falta " + path_text(tc.vcvars) + "\n\n" + kToolchainHelp);
    }
    say("compilador: " + path_text(tc.installRoot));

    // ambiente do vcvars: "set" do cmd em UTF-16 (/u). O caminho vai por variavel de
    // ambiente: escrito na linha de comando, o cmd expandiria % e interpretaria & ^ (revisao
    // Codex 2026-10-14); o conteudo de uma variavel expandida entre aspas nao e reinterpretado.
    // Sem "call": ele faz uma segunda expansao de % (testado). Fora de um .bat, o cmd volta
    // da execucao do .bat e segue para o "&& set".
    Env vcEnv              = current_env();
    vcEnv[L"BS_VCVARS_PATH"] = tc.vcvars.native();
    const std::wstring cmd = L"cmd.exe /d /u /s /c \"\"%BS_VCVARS_PATH%\" >nul 2>nul && set\"";
    std::vector<std::string> out;
    const auto r = run_process(cmd, {}, vcEnv, [&](const std::string& l) { out.push_back(l); }, nullptr, true, 120000);
    if (!r.started || r.exitCode != 0) throw std::runtime_error("vcvars64.bat falhou (codigo " + std::to_string(r.exitCode) + ")");
    for (const auto& l : out) {
        const size_t eq = l.find('=', 1);
        if (eq != std::string::npos) tc.env[widen(l.substr(0, eq))] = widen(l.substr(eq + 1));
    }
    tc.env.erase(L"BS_VCVARS_PATH");
    const std::wstring tools = envGet(tc.env, L"VCToolsInstallDir");
    const std::wstring sdk   = envGet(tc.env, L"WindowsSdkDir");
    const std::wstring redist = envGet(tc.env, L"VCToolsRedistDir");
    if (tools.empty()) throw std::runtime_error("o vcvars64.bat nao configurou o compilador C++ x64.\n\n" + std::string(kToolchainHelp));
    if (sdk.empty()) throw std::runtime_error("SDK do Windows nao encontrado na instalacao.\n\n" + std::string(kToolchainHelp));
    tc.msvcVersion = trim(narrow(envGet(tc.env, L"VCToolsVersion")));
    tc.sdkVersion  = trim(narrow(envGet(tc.env, L"WindowsSDKVersion")));
    if (findOnPath(tc.env, L"cl.exe").empty()) throw std::runtime_error("cl.exe nao esta no PATH do vcvars");

    // CMake e Ninja: os que vem com o Visual Studio; senao, os do PATH
    const fs::path cmakeExt = tc.installRoot / L"Common7" / L"IDE" / L"CommonExtensions" / L"Microsoft" / L"CMake";
    tc.cmake = cmakeExt / L"CMake" / L"bin" / L"cmake.exe";
    tc.ninja = cmakeExt / L"Ninja" / L"ninja.exe";
    if (!fs::is_regular_file(tc.cmake, ec)) tc.cmake = findOnPath(tc.env, L"cmake.exe");
    if (!fs::is_regular_file(tc.ninja, ec)) tc.ninja = findOnPath(tc.env, L"ninja.exe");
    if (tc.cmake.empty() || tc.ninja.empty())
        throw std::runtime_error("CMake ou Ninja nao encontrado (componente \"Ferramentas CMake do C++\").\n\n" +
                                 std::string(kToolchainHelp));

    // runtime do MSVC (instalacao local ao lado do jogo)
    if (!redist.empty()) {
        for (const auto& e : fs::directory_iterator(fs::path(redist) / L"x64", ec)) {
            const std::wstring n = e.path().filename().native();
            if (e.is_directory() && n.rfind(L"Microsoft.VC", 0) == 0 && n.size() > 4 && n.substr(n.size() - 4) == L".CRT") {
                tc.crtDir = e.path();
                break;
            }
        }
    }
    if (tc.crtDir.empty()) throw std::runtime_error("runtime redistribuivel do MSVC (Microsoft.VC*.CRT) nao encontrado");
    say("MSVC " + tc.msvcVersion + ", SDK do Windows " + tc.sdkVersion);
    say("cmake: " + path_text(tc.cmake));
    say("ninja: " + path_text(tc.ninja));
    return tc;
}

} // namespace bs::builder
