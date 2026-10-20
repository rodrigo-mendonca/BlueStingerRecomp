// Builder (builder/): aspas de linha de comando, caminhos fora da pagina ANSI, processos
// filhos (codigo, UTF-16, cancelamento, tempo limite), troca segura da versao instalada e
// recusa de entradas invalidas. Sem compilador nem dados do jogo.
#include "test.h"

#include "builder_core.h"
#include "child_process.h"
#include "toolchain.h"
#include "platform/install_config.h"

#include <optional>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bs::builder;

namespace {

// pasta temporaria com espacos e caracteres fora da pagina ANSI; apagada no fim
struct TempDir {
    fs::path p;
    TempDir() {
        p = fs::temp_directory_path() / (L"bs unit builder ç 日本 " + std::to_wstring(GetCurrentProcessId()));
        std::error_code ec;
        fs::remove_all(p, ec);
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};

void writeFile(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

// variavel de ambiente trocada so durante o teste
struct EnvGuard {
    std::wstring name, old;
    bool         had;
    EnvGuard(const wchar_t* n, const std::wstring& v) : name(n) {
        wchar_t     b[32768];
        const DWORD k = GetEnvironmentVariableW(n, b, 32768);
        had = k > 0;
        if (had) old.assign(b, k);
        SetEnvironmentVariableW(n, v.c_str());
    }
    ~EnvGuard() { SetEnvironmentVariableW(name.c_str(), had ? old.c_str() : nullptr); }
};

bool isPkg(const fs::path& rel) {
    const std::wstring f = rel.begin()->native();
    return f == L"BlueStinger.exe" || f == L"licencas";
}

} // namespace

BS_TEST(builder_quote_arg_roundtrip) {
    const std::vector<std::wstring> cases = {L"simples", L"com espaço", L"aspas \"dentro\"", L"barra\\fim\\",
                                             L"C:\\Pasta (x86)\\a b\\", L"", L"日本 ç", L"\\\\servidor\\\"x\""};
    for (const auto& s : cases) {
        const std::wstring cmd  = L"prog.exe " + quote_arg(s) + L" fim";
        int                argc = 0;
        wchar_t**          argv = CommandLineToArgvW(cmd.c_str(), &argc);
        BS_CHECK(argc == 3 && argv[1] == s && std::wstring(argv[2]) == L"fim");
        LocalFree(argv);
    }
}

BS_TEST(builder_ansi_safe_paths) {
    BS_CHECK(ansi_safe(L"C:\\Jogos\\Blue Stinger"));
    BS_CHECK(!ansi_safe(L"C:\\Jogos\\\U0001F600"));  // nenhuma pagina ANSI tem emoji
    BS_CHECK(path_text(L"C:\\日本 ç") == "C:\\\xE6\x97\xA5\xE6\x9C\xAC \xC3\xA7");
}

BS_TEST(builder_run_process_exit_code_and_output) {
    std::vector<std::string> lines;
    const auto r = run_process(L"cmd.exe /d /c \"echo linha 1& echo linha 2& exit 7\"", {}, {},
                               [&](const std::string& l) { lines.push_back(l); });
    BS_CHECK(r.started && !r.cancelled && !r.timedOut && r.exitCode == 7);
    BS_CHECK(lines.size() == 2 && lines[0] == "linha 1" && lines[1] == "linha 2");

    // saida UTF-16 (cmd /u) com caracteres fora da pagina ANSI
    lines.clear();
    const auto u = run_process(L"cmd.exe /d /u /c \"echo 日本 ç\"", {}, {}, [&](const std::string& l) { lines.push_back(l); },
                               nullptr, true);
    BS_CHECK(u.exitCode == 0 && lines.size() == 1 && lines[0] == path_text(L"日本 ç"));

    // ambiente proprio
    Env env = current_env();
    env[L"BS_TESTE_VAR"] = L"valor ç";
    lines.clear();
    run_process(L"cmd.exe /d /u /c \"echo %BS_TESTE_VAR%\"", {}, env, [&](const std::string& l) { lines.push_back(l); },
                nullptr, true);
    BS_CHECK(lines.size() == 1 && lines[0] == path_text(L"valor ç"));

    // programa inexistente: nao iniciado
    const auto n = run_process(L"nao_existe_bs_teste.exe", {}, {}, nullptr);
    BS_CHECK(!n.started);
}

std::vector<DWORD> pingPids() {
    std::vector<DWORD> r;
    HANDLE             s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W    e{sizeof e};
    for (BOOL ok = Process32FirstW(s, &e); ok; ok = Process32NextW(s, &e))
        if (_wcsicmp(e.szExeFile, L"PING.EXE") == 0) r.push_back(e.th32ProcessID);
    CloseHandle(s);
    return r;
}

// Processo que sai logo depois de escrever (como "BlueStinger.exe --version"): a saida nunca
// pode se perder, mesmo com muitas execucoes seguidas (falha intermitente vista no
// test_builder: codigo 0 sem a linha de versao).
BS_TEST(builder_run_process_short_lived_output_is_never_lost) {
    int lost = 0;
    for (int i = 0; i < 300; ++i) {
        std::string first;
        const auto  r = run_process(L"cmd.exe /d /c \"echo linha unica\"", {}, {}, [&](const std::string& l) {
            if (first.empty()) first = l;
        });
        if (r.exitCode != 0 || first != "linha unica") ++lost;
    }
    BS_CHECK(lost == 0);
    if (lost) std::fprintf(stderr, "  saidas perdidas: %d de 300\n", lost);
}

BS_TEST(builder_run_process_cancel_and_timeout_kill_tree) {
    using clock = std::chrono::steady_clock;
    const std::vector<DWORD> before = pingPids();
    auto                     noNewPing = [&] {
        for (DWORD p : pingPids())
            if (std::find(before.begin(), before.end(), p) == before.end()) return false;
        return true;
    };
    // cmd -> ping (neto): cancelar e o tempo limite encerram a arvore inteira, rapido
    std::atomic<bool> cancel{true};
    auto              t0 = clock::now();
    const auto        c  = run_process(L"cmd.exe /d /c \"ping -n 30 127.0.0.1 >nul\"", {}, {}, nullptr, &cancel);
    BS_CHECK(c.cancelled && c.exitCode != 0);
    BS_CHECK(clock::now() - t0 < std::chrono::seconds(10));
    t0           = clock::now();
    const auto t = run_process(L"cmd.exe /d /c \"ping -n 30 127.0.0.1 >nul\"", {}, {}, nullptr, nullptr, false, 300);
    BS_CHECK(t.timedOut && t.exitCode != 0);
    BS_CHECK(clock::now() - t0 < std::chrono::seconds(10));
    BS_CHECK(noNewPing());  // o neto (ping) morreu junto (job object)
}

BS_TEST(builder_install_swap_keeps_user_files_and_rolls_back) {
    TempDir        tmp;
    const fs::path dest = tmp.p / L"BlueStinger";
    auto           stage = [&](const std::string& ver) {
        const fs::path s = tmp.p / (L"BlueStinger.new-" + bs::builder::widen(ver));
        writeFile(s / L"BlueStinger.exe", ver);
        writeFile(s / L"licencas" / L"MIT.txt", "mit " + ver);
        if (ver == "v1") writeFile(s / L"licencas" / L"so_v1.txt", "pacote v1");
        return s;
    };

    // 1a instalacao
    install_swap(stage("v1"), dest, isPkg);
    BS_CHECK(readFile(dest / L"BlueStinger.exe") == "v1");

    // arquivos do usuario sobrevivem (inclusive uma DLL dele e um arquivo dentro de licencas);
    // os do pacote anterior saem, pela lista dele; nada sobra
    BS_CHECK(fs::exists(dest / kPackageManifest) && fs::exists(dest / kInstallMarker));
    writeFile(dest / L"bluestinger.ini", "cdi=ç");
    writeFile(dest / L"saves" / L"vmu.bin", "save");
    writeFile(dest / L"licencas" / L"minha.txt", "fica");
    writeFile(dest / L"minha.dll", "fica");
    install_swap(stage("v2"), dest, isPkg);
    BS_CHECK(readFile(dest / L"BlueStinger.exe") == "v2");
    BS_CHECK(readFile(dest / L"bluestinger.ini") == "cdi=ç");
    BS_CHECK(readFile(dest / L"saves" / L"vmu.bin") == "save");
    BS_CHECK(readFile(dest / L"licencas" / L"minha.txt") == "fica" && readFile(dest / L"minha.dll") == "fica");
    BS_CHECK(!fs::exists(dest / L"licencas" / L"so_v1.txt"));
    // a versao anterior fica para rollback; nenhuma outra sobra
    size_t dirs = 0;
    for (const auto& e : fs::directory_iterator(tmp.p)) dirs += e.is_directory();
    BS_CHECK(dirs == 2 && readFile(tmp.p / L"BlueStinger.anterior" / L"BlueStinger.exe") == "v1");

    // jogo aberto (arquivo em uso sem compartilhar exclusao): a troca falha e a v2 continua
    const fs::path s3 = stage("v3");
    HANDLE h = CreateFileW((dest / L"BlueStinger.exe").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    BS_CHECK(h != INVALID_HANDLE_VALUE);
    bool threw = false;
    try {
        install_swap(s3, dest, isPkg);
    } catch (const std::exception&) {
        threw = true;
    }
    CloseHandle(h);
    BS_CHECK(threw);
    BS_CHECK(readFile(dest / L"BlueStinger.exe") == "v2" && readFile(dest / L"bluestinger.ini") == "cdi=ç");

    // troca interrompida (sem dest, so o backup): o backup volta antes da nova entrar
    fs::rename(dest, tmp.p / L"BlueStinger.old-deadbeef");
    install_swap(s3, dest, isPkg);
    BS_CHECK(readFile(dest / L"BlueStinger.exe") == "v3" && readFile(dest / L"bluestinger.ini") == "cdi=ç");
    BS_CHECK(!fs::exists(tmp.p / L"BlueStinger.old-deadbeef"));
}

// instalacao.toml (modo portatil) e da instalacao: escolhido, fica; recompilar sem escolha (Developer,
// CLI) o preserva; trocar o modo regrava so ele, sem tocar nos saves de nenhum dos dois lugares
BS_TEST(builder_install_swap_portable_config) {
    namespace ni = bs::native::install;
    TempDir        tmp;
    const fs::path dest = tmp.p / L"BlueStinger";
    auto           stage = [&](const std::string& ver, std::optional<bool> portable) {
        const fs::path s = tmp.p / (L"BlueStinger.new-" + bs::builder::widen(ver));
        writeFile(s / L"BlueStinger.exe", ver);
        if (portable) ni::write_config(s, {*portable ? ni::SavesLocation::Portable : ni::SavesLocation::Documents});
        return s;
    };
    BS_CHECK(!inspect_install(tmp.p).exists);
    install_swap(stage("v1", true), dest, isPkg);
    BS_CHECK(ni::read_config(dest).saves == ni::SavesLocation::Portable);
    const InstallInfo i = inspect_install(tmp.p);
    BS_CHECK(i.exists && i.ours && i.portable && i.previous == tmp.p / L"BlueStinger.anterior");
    BS_CHECK(readFile(dest / kPackageManifest).find("instalacao.toml") == std::string::npos);  // fora da lista
    writeFile(dest / L"saves" / L"card0" / L"BLUESTIN.000.bsv", "partida");
    install_swap(stage("v2", std::nullopt), dest, isPkg);  // sem escolha: o arquivo anterior fica
    BS_CHECK(readFile(dest / L"BlueStinger.exe") == "v2" && ni::read_config(dest).saves == ni::SavesLocation::Portable);
    install_swap(stage("v3", false), dest, isPkg);  // troca para Documentos: a partida portatil continua
    BS_CHECK(ni::read_config(dest).saves == ni::SavesLocation::Documents && !inspect_install(tmp.p).portable);
    BS_CHECK(readFile(dest / L"saves" / L"card0" / L"BLUESTIN.000.bsv") == "partida");
    // pasta alheia (sem a nossa marca) no lugar da instalacao
    const fs::path other = tmp.p / L"outra";
    writeFile(other / L"BlueStinger" / L"leia.txt", "meu");
    const InstallInfo f = inspect_install(other);
    BS_CHECK(f.exists && !f.ours && !f.portable);
}

BS_TEST(builder_install_swap_recovery_and_cancel) {
    TempDir        tmp;
    const fs::path dest = tmp.p / L"BlueStinger";
    auto           backup = [&](const wchar_t* name, const char* exe, const char* marker) {
        const fs::path b = tmp.p / name;
        writeFile(b / L"BlueStinger.exe", exe);
        if (marker) writeFile(b / kInstallMarker, marker);
        return b;
    };
    // varias sobras sem dest: volta a de marca mais recente; a sem marca nao e tocada; a
    // outra marcada sai (sobra de troca concluida)
    backup(L"BlueStinger.old-aaaa", "antiga", "100");
    backup(L"BlueStinger.old-bbbb", "recente", "200");
    backup(L"BlueStinger.old-cccc", "alheia", nullptr);
    const fs::path s1 = tmp.p / L"BlueStinger.new-1";
    writeFile(s1 / L"BlueStinger.exe", "nova");
    std::atomic<bool> cancel{true};
    bool              cancelled = false;
    try {
        install_swap(s1, dest, isPkg, &cancel);
    } catch (const SwapCancelled&) {
        cancelled = true;
    }
    // cancelado antes da troca: a recuperada fica no lugar, o pacote novo nao entrou
    BS_CHECK(cancelled && readFile(dest / L"BlueStinger.exe") == "recente" && fs::exists(s1));
    BS_CHECK(!fs::exists(tmp.p / L"BlueStinger.old-aaaa") && !fs::exists(tmp.p / L"BlueStinger.old-bbbb"));
    BS_CHECK(readFile(tmp.p / L"BlueStinger.old-cccc" / L"BlueStinger.exe") == "alheia");
    cancel = false;
    install_swap(s1, dest, isPkg, &cancel);
    BS_CHECK(readFile(dest / L"BlueStinger.exe") == "nova" && !fs::exists(s1));
    BS_CHECK(readFile(tmp.p / L"BlueStinger.anterior" / L"BlueStinger.exe") == "recente");
    BS_CHECK(fs::exists(tmp.p / L"BlueStinger.old-cccc"));  // nunca apagamos o que nao e nosso

    // .anterior ocupada por algo sem a nossa marca: fica intacta; a versao anterior vai para
    // .anterior-<data> (e nao some na proxima execucao)
    fs::remove_all(tmp.p / L"BlueStinger.anterior");
    writeFile(tmp.p / L"BlueStinger.anterior" / L"BlueStinger.exe", "alheia");
    const fs::path s2 = tmp.p / L"BlueStinger.new-2";
    writeFile(s2 / L"BlueStinger.exe", "mais nova");
    install_swap(s2, dest, isPkg);
    BS_CHECK(readFile(dest / L"BlueStinger.exe") == "mais nova");
    BS_CHECK(readFile(tmp.p / L"BlueStinger.anterior" / L"BlueStinger.exe") == "alheia");
    size_t kept = 0;
    for (const auto& e : fs::directory_iterator(tmp.p))
        if (e.path().filename().native().rfind(L"BlueStinger.anterior-", 0) == 0)
            kept += readFile(e.path() / L"BlueStinger.exe") == "nova";
    BS_CHECK(kept == 1);

    // .anterior nossa, mas presa (arquivo aberto sem compartilhar exclusao): nao da para tira-la
    // do caminho; a versao anterior vai para um nome unico e nada se perde
    fs::remove_all(tmp.p / L"BlueStinger.anterior");
    writeFile(tmp.p / L"BlueStinger.anterior" / L"BlueStinger.exe", "presa");
    writeFile(tmp.p / L"BlueStinger.anterior" / std::wstring(kInstallMarker), "1\n");
    HANDLE lock = CreateFileW((tmp.p / L"BlueStinger.anterior" / L"BlueStinger.exe").c_str(), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    const fs::path s3 = tmp.p / L"BlueStinger.new-3";
    writeFile(s3 / L"BlueStinger.exe", "terceira");
    bool threw = false;
    try {
        install_swap(s3, dest, isPkg);
    } catch (const std::exception&) {
        threw = true;
    }
    CloseHandle(lock);
    BS_CHECK(!threw && readFile(dest / L"BlueStinger.exe") == "terceira");
    BS_CHECK(readFile(tmp.p / L"BlueStinger.anterior" / L"BlueStinger.exe") == "presa");
    size_t archived = 0;
    for (const auto& e : fs::directory_iterator(tmp.p))
        if (e.path().filename().native().rfind(L"BlueStinger.anterior-", 0) == 0)
            archived += readFile(e.path() / L"BlueStinger.exe") == "mais nova";
    BS_CHECK(archived == 1);
}

// Compilador falso: vcvars64.bat num caminho com % & ( ) ^ (o cmd expandiria %TEMP% e
// partiria em & se o caminho fosse escrito na linha de comando).
BS_TEST(builder_find_toolchain_fake_vcvars_special_path) {
    const fs::path root = fs::temp_directory_path() /
                          (L"bs tc %TEMP% & (x) ^ " + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path build = root / L"VC" / L"Auxiliary" / L"Build";
    writeFile(build / L"vcvars64.bat",
              "@set \"VCToolsInstallDir=%~dp0\"\r\n@set \"WindowsSdkDir=%~dp0\"\r\n@set \"VCToolsVersion=14.99.1\"\r\n"
              "@set \"WindowsSDKVersion=10.0.1\\\"\r\n@set \"VCToolsRedistDir=%~dp0redist\\\"\r\n"
              "@set \"PATH=%~dp0bin;%PATH%\"\r\n");
    for (const wchar_t* f : {L"bin/cl.exe", L"bin/cmake.exe", L"bin/ninja.exe"}) writeFile(build / f, "");
    fs::create_directories(build / L"redist" / L"x64" / L"Microsoft.VC999.CRT");
    std::string err;
    Toolchain   tc;
    try {
        tc = find_toolchain({{}, build / L"vcvars64.bat"}, nullptr);
    } catch (const std::exception& e) {
        err = e.what();
    }
    BS_CHECK(err.empty());
    BS_CHECK(tc.msvcVersion == "14.99.1" && tc.sdkVersion == "10.0.1");
    BS_CHECK(tc.cmake == build / L"bin" / L"cmake.exe" && tc.ninja == build / L"bin" / L"ninja.exe");
    BS_CHECK(tc.crtDir.filename() == L"Microsoft.VC999.CRT");
    BS_CHECK(tc.env.count(L"BS_VCVARS_PATH") == 0 && tc.env.count(L"VCToolsInstallDir") == 1);
    if (!err.empty()) std::fprintf(stderr, "  toolchain falso: %s\n", err.c_str());

    // vcvars que nao configura o compilador: erro claro (sem herdar o ambiente de um prompt
    // de desenvolvedor em que o teste rode)
    EnvGuard g1(L"VCToolsInstallDir", L""), g2(L"WindowsSdkDir", L""), g3(L"VCToolsRedistDir", L"");
    writeFile(build / L"vcvars64.bat", "@rem nada\r\n");
    err.clear();
    try {
        find_toolchain({{}, build / L"vcvars64.bat"}, nullptr);
    } catch (const std::exception& e) {
        err = e.what();
    }
    BS_CHECK(err.find("nao configurou o compilador") != std::string::npos);
    fs::remove_all(root, ec);
}

BS_TEST(builder_rejects_bad_inputs_without_side_effects) {
    TempDir        tmp;
    EnvGuard       cache(L"BS_BUILDER_CACHE", (tmp.p / L"cache").native());
    const fs::path disc = tmp.p / L"imagem ç.cdi";
    writeFile(disc, std::string(4096, '\0'));
    // kit minimo (so os arquivos que check_kit exige)
    const fs::path kit = tmp.p / L"kit";
    for (const wchar_t* f : {L"CMakeLists.txt", L"LICENSE", L"config/bluestinger_us.toml", L"app/CMakeLists.txt",
                             L"runtime/CMakeLists.txt", L"tools/extractor/CMakeLists.txt", L"tools/dist/LEIA-ME.txt",
                             L"tools/dcmap/CMakeLists.txt", L"native/CMakeLists.txt", L"legacy/CMakeLists.txt",
                             L"transitional/CMakeLists.txt", L"tools/build/msvc_srcdeps.cmake",
                             L"third_party/SDL3/CMakeLists.txt"})
        writeFile(kit / f, "");
    // N3-1: sem as fontes do toml++ (nem no kit nem ao lado do SDL3) o kit esta incompleto
    BS_CHECK(check_kit(kit, kit / L"third_party" / L"SDL3").find("toml++") != std::string::npos);
    writeFile(kit / L"third_party/tomlplusplus/CMakeLists.txt", "");
    BS_CHECK(check_kit(kit, kit / L"third_party" / L"SDL3").empty());
    BS_CHECK(toml_dir(kit, kit / L"third_party" / L"SDL3") == kit / L"third_party" / L"tomlplusplus");
    BS_CHECK(toml_dir(tmp.p / L"repo", tmp.p / L"build" / L"_deps" / L"sdl3-src") ==
             tmp.p / L"build" / L"_deps" / L"tomlplusplus-src");
    BS_CHECK(!check_kit(tmp.p / L"vazio", tmp.p / L"vazio").empty());

    auto run = [&](Request r) {
        r.vswhere = tmp.p / L"nao existe" / L"vswhere.exe";  // nunca acha o compilador
        return build(r, {});
    };
    const fs::path out = tmp.p / L"saída ç";
    Request        base;
    base.disc   = disc;
    base.outDir = out;
    base.kitDir = kit;

    Request r1 = base;
    r1.disc    = tmp.p / L"nao existe.cdi";
    Result a   = run(r1);
    BS_CHECK(!a.ok && a.failedStep == Step::Validate && a.error.find("nao encontrada") != std::string::npos);

    Request r2 = base;
    r2.kitDir  = tmp.p / L"vazio";
    Result b   = run(r2);
    BS_CHECK(!b.ok && b.failedStep == Step::Validate && b.error.find("kit") != std::string::npos);

    Request r3 = base;
    r3.outDir  = tmp.p / std::wstring(220, L'x');
    Result c   = run(r3);
    BS_CHECK(!c.ok && c.failedStep == Step::Validate && c.error.find("longo demais") != std::string::npos);

    Request r4 = base;
    r4.outDir  = disc / L"sub";  // dentro de um arquivo: nao da para criar
    Result d   = run(r4);
    BS_CHECK(!d.ok && d.failedStep == Step::Validate);

    Result e = run(base);  // entradas ok, sem compilador
    BS_CHECK(!e.ok && e.failedStep == Step::Toolchain && e.error.find("Build Tools") != std::string::npos);
    BS_CHECK(fs::exists(e.logFile) && readFile(e.logFile).find("ERRO") != std::string::npos);

    // nenhuma tentativa criou o jogo instalado nem pacote temporario
    BS_CHECK(!fs::exists(out / L"BlueStinger"));
    for (const auto& x : fs::directory_iterator(out)) BS_CHECK(x.path().filename() == L".bs-build");
}
