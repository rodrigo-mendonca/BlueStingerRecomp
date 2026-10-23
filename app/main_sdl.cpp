// bluestinger: executavel nativo. Janela, apresentacao e eventos pelo SDL3 (so neste
// arquivo); o jogo roda no GameHost (outra thread). Os dados do jogo vem da pasta data/
// ao lado do executavel, preparada pelo Builder a partir do CDI do usuario: o jogo NAO le
// o CDI (nem precisa dele depois da conversao).
//
//   bluestinger [--data <pasta>] [--flash <arquivo>] [--map <arquivo>] [--scale N] [--capture <dir>]
//   bluestinger [--data <pasta>] --headless <vblanks> [--dump <arquivo.bmp>]   (sem janela)
//   bluestinger [--data <pasta>] --verify-data   (confere o SHA-256 de todos os arquivos)
//   bluestinger --version
// Caminhos em UTF-16/UTF-8 (nomes com acentos ou outros alfabetos funcionam). Subsistema
// grafico: sem console, o log vai para bluestinger.log ao lado do executavel (ou em
// %LOCALAPPDATA%\BlueStingerRecomp).
#include "boot/native_boot.h"
#include "runtime_data.h"
#include "assist.h"
#include "bs_version.h"
#include "disc/disc_image.h"
#include "funcs.h"
#include "game_host.h"
#include "subtitle_overlay.h"
#include "menu_overlay.h"
#include "window_present.h"
#include "settings_menu.h"
#include "localization/mod_packages.h"
#include "platform/camera_settings.h"
#include "platform/graphics_settings.h"
#include "platform/user_settings.h"
#include "render/vulkan/vk_device.h"
#include "gdfs_adapter.h"
#include "input/input.h"
#include "input_bridge.h"
#include "debug/file_log.h"
#include "pad_contract.h"
#include "platform/install_config.h"
#include "platform/input.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")  // SHGetKnownFolderPath (pasta dos saves)
#pragma comment(lib, "ole32.lib")    // CoTaskMemFree

namespace fs = std::filesystem;
namespace ni = bs::native::input;
namespace ib = bs::transitional::input_bridge;

namespace {

const char* kTitle = "Blue Stinger (recompilado)";

// N6-H: o padrao dos tres provedores de audio (efeitos, falas, musica): NATIVO desde a fase H (docs/N6-PLANO.md secoes
// 20.5 e 20.6; H1-H9 aprovados, o H9 com o Vulkan padrao desde 2026-10-23). O legado continua como fallback ate
// o N8: --audio legado ou BS_AUDIO=legado; --audio-efeitos/-falas/-musica valem sobre os dois.
constexpr bool kAudioNativeDefault = true;
// N7 (fechamento, docs/N7-PLANO.md secao 25): o renderizador padrao da janela e o VULKAN, com recuo seguro para o
// software (sem dispositivo na partida: o menu fica no software; perda do dispositivo no jogo: ate 3 recriacoes e
// depois o software ate o fim da sessao). O --headless (diagnostico e baterias) continua no software, a referencia.
// --render software|nativo ou BS_RENDER valem sobre o padrao; a opcao vale sobre a variavel.
constexpr bool kRenderNativeDefault = true;

struct Args {
    fs::path gameData, flash, map, dump, capture;
    fs::path positional;  // argumento solto (antes: a imagem CDI) -> mensagem explicando
    uint64_t headless   = 0;
    uint64_t dumpEvery  = 0;  // headless: quadro a cada N VBlanks em --capture
    int      scale      = 2;
    bool     version    = false;
    bool     verifyData = false;
    bs::FilesProvider files = bs::FilesProvider::Native;  // --arquivos nativo|legado (N1)
    bs::TimeProviderKind time = bs::TimeProviderKind::Native;  // --tempo nativo|legado (N2a)
    bs::ControlsProvider controls = bs::ControlsProvider::Native;  // --controles nativo|legado (N3; legado = fallback)
    bs::SavesProvider saves = bs::SavesProvider::Native;  // --saves nativo|legado (N4; legado = fallback)
    fs::path savesDir;          // --saves-pasta <dir>
    bs::VideoProvider video = bs::VideoProvider::Native;  // --video nativo|legado (N5; legado = fallback)
    int render = -1;  // --render nativo|software (N7b; -1 = nao dado)
    bool renderExplicit = false;  // --render ou BS_RENDER dados (valem sobre o graficos.toml do menu)
    uint32_t internalHeight = 0;  // --resolucao-interna <altura> (N7c; 0 = a do graficos.toml)
    int      cameraMode = -1;     // --camera original|livre (N7c; -1 = a do controles.toml, ou Original sem janela)
    int      present = -1;        // --apresentacao vulkan|sdl (N7c; -1 = Vulkan quando houver)
    bs::AudioEffectsProvider audioEffects = bs::AudioEffectsProvider::Legacy;  // --audio-efeitos nativo|legado (N6)
    bs::AudioVoicesProvider  audioVoices  = bs::AudioVoicesProvider::Legacy;   // --audio-falas nativo|legado (N6 fase E)
    bs::AudioMusicProvider   audioMusic   = bs::AudioMusicProvider::Legacy;    // --audio-musica nativo|legado (N6 fase F)
    std::string              dubbing;                                         // --dublagem <idioma> (mods\audio)
    int                      audioAll = -1;  // --audio nativo|legado (os tres provedores; -1 = nao dado)
    bool                     effectsSet = false, voicesSet = false, musicSet = false;  // --audio-* explicitos (valem sobre --audio)
    std::string       subtitles;  // --legendas <idioma>|nenhuma
    bool              subtitlesSet = false;
    bool     savesPortable = false;  // --saves-portatil: saves\ ao lado do executavel
    fs::path savesImport, savesExport;       // --saves-importar/--saves-exportar <arquivo.vms>
    std::string savesName = "BLUESTIN.000";  // --saves-nome (slot 1 do jogo; 2-6 = .001-.005)
};

std::string utf8(const fs::path& p) { return bs::disc::pathText(p); }

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// Argumentos em UTF-16 (o argv de main usa a pagina de codigo ANSI e perde caracteres).
bool parseArgs(Args& a) {
    int       argc  = 0;
    LPWSTR*   wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) return false;
    std::vector<std::wstring> args(wargv + 1, wargv + argc);
    LocalFree(wargv);
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& s    = args[i];
        auto                next = [&]() -> const std::wstring* { return i + 1 < args.size() ? &args[++i] : nullptr; };
        const std::wstring* v    = nullptr;
        if (s == L"--flash" && (v = next())) a.flash = *v;
        else if (s == L"--map" && (v = next())) a.map = *v;
        else if (s == L"--dump" && (v = next())) a.dump = *v;
        else if (s == L"--capture" && (v = next())) a.capture = *v;
        else if (s == L"--dump-every" && (v = next())) a.dumpEvery = std::wcstoull(v->c_str(), nullptr, 10);
        else if (s == L"--headless" && (v = next())) a.headless = std::wcstoull(v->c_str(), nullptr, 10);
        else if (s == L"--scale" && (v = next())) a.scale = _wtoi(v->c_str());
        else if (s == L"--data" && (v = next())) a.gameData = *v;
        else if (s == L"--arquivos" && (v = next())) {
            if (*v == L"nativo") a.files = bs::FilesProvider::Native;
            else if (*v == L"legado") a.files = bs::FilesProvider::Legacy;
            else return false;
        }
        else if (s == L"--tempo" && (v = next())) {
            if (*v == L"nativo") a.time = bs::TimeProviderKind::Native;
            else if (*v == L"legado") a.time = bs::TimeProviderKind::Legacy;
            else if (*v == L"experimental") a.time = bs::TimeProviderKind::Experimental;  // N2b-2
            else return false;
        }
        else if (s == L"--controles" && (v = next())) {
            if (*v == L"legado") a.controls = bs::ControlsProvider::Legacy;
            else if (*v == L"nativo") a.controls = bs::ControlsProvider::Native;
            else return false;
        }
        else if (s == L"--saves" && (v = next())) {
            if (*v == L"legado") a.saves = bs::SavesProvider::Legacy;
            else if (*v == L"nativo") a.saves = bs::SavesProvider::Native;
            else return false;
        }
        else if (s == L"--saves-pasta" && (v = next())) a.savesDir = *v;
        else if (s == L"--video" && (v = next())) {
            if (*v == L"legado") a.video = bs::VideoProvider::Legacy;
            else if (*v == L"nativo") a.video = bs::VideoProvider::Native;
            else return false;
        }
        else if (s == L"--render" && (v = next())) {
            if (*v == L"software") a.render = 0;
            else if (*v == L"nativo") a.render = 1;
            else return false;
        }
        else if (s == L"--legendas" && (v = next())) a.subtitles = utf8(fs::path(*v)), a.subtitlesSet = true;
        else if (s == L"--apresentacao" && (v = next())) {
            if (*v == L"vulkan") a.present = 1;
            else if (*v == L"sdl") a.present = 0;
            else return false;
        }
        else if (s == L"--camera" && (v = next())) {
            if (*v == L"original") a.cameraMode = 0;
            else if (*v == L"livre" || *v == L"hibrida") a.cameraMode = 1;  // hibrida: modo retirado (2026-10-23)
            else return false;
        }
        else if (s == L"--resolucao-interna" && (v = next())) {
            a.internalHeight = uint32_t(std::wcstoul(v->c_str(), nullptr, 10));
            if (a.internalHeight < 240 || a.internalHeight > 4320) return false;
        }
        else if (s == L"--audio" && (v = next())) {
            if (*v == L"legado") a.audioAll = 0;
            else if (*v == L"nativo") a.audioAll = 1;
            else return false;
        }
        else if (s == L"--audio-falas" && (v = next())) {
            a.voicesSet = true;
            if (*v == L"legado") a.audioVoices = bs::AudioVoicesProvider::Legacy;
            else if (*v == L"nativo") a.audioVoices = bs::AudioVoicesProvider::Native;
            else return false;
        }
        else if (s == L"--dublagem" && (v = next())) a.dubbing = utf8(fs::path(*v));
        else if (s == L"--audio-musica" && (v = next())) {
            a.musicSet = true;
            if (*v == L"legado") a.audioMusic = bs::AudioMusicProvider::Legacy;
            else if (*v == L"nativo") a.audioMusic = bs::AudioMusicProvider::Native;
            else return false;
        }
        else if (s == L"--audio-efeitos" && (v = next())) {
            a.effectsSet = true;
            if (*v == L"legado") a.audioEffects = bs::AudioEffectsProvider::Legacy;
            else if (*v == L"nativo") a.audioEffects = bs::AudioEffectsProvider::Native;
            else return false;
        }
        else if (s == L"--saves-portatil") a.savesPortable = true;
        else if (s == L"--saves-importar" && (v = next())) a.savesImport = *v;
        else if (s == L"--saves-exportar" && (v = next())) a.savesExport = *v;
        else if (s == L"--saves-nome" && (v = next())) a.savesName = utf8(*v);
        else if (s == L"--version") a.version = true;
        else if (s == L"--verify-data") a.verifyData = true;
        else if (!s.empty() && s[0] != L'-' && a.positional.empty()) a.positional = s;
        else return false;
    }
    bool native = kAudioNativeDefault;
    if (const char* e = std::getenv("BS_AUDIO"); e && *e) {
        if (std::strcmp(e, "nativo") == 0) native = true;
        else if (std::strcmp(e, "legado") == 0) native = false;
        else return false;
    }
    if (a.audioAll >= 0) native = a.audioAll == 1;
    bool vk = kRenderNativeDefault && !a.headless;
    if (const char* e = std::getenv("BS_RENDER"); e && *e) {
        if (std::strcmp(e, "nativo") == 0) vk = true;
        else if (std::strcmp(e, "software") == 0) vk = false;
        else return false;
    }
    a.renderExplicit = a.render >= 0 || (std::getenv("BS_RENDER") && *std::getenv("BS_RENDER"));
    if (a.render < 0) a.render = vk ? 1 : 0;
    if (!a.effectsSet) a.audioEffects = native ? bs::AudioEffectsProvider::Native : bs::AudioEffectsProvider::Legacy;
    if (!a.voicesSet) a.audioVoices = native ? bs::AudioVoicesProvider::Native : bs::AudioVoicesProvider::Legacy;
    if (!a.musicSet) a.audioMusic = native ? bs::AudioMusicProvider::Native : bs::AudioMusicProvider::Legacy;
    return a.scale >= 1 && a.scale <= 8;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int    n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// Caixa de mensagem nativa (antes do SDL ou se ele falhar), em UTF-16.
void nativeBox(const std::string& msg, UINT icon) { MessageBoxW(nullptr, widen(msg).c_str(), widen(kTitle).c_str(), icon); }

fs::path exeDir() {
    std::wstring buf(32768, L'\0');
    const DWORD  n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    buf.resize(n);
    return fs::path(buf).parent_path();
}

// Pasta dos arquivos do usuario (log): ao lado do executavel se der para gravar la; senao
// %LOCALAPPDATA%\BlueStingerRecomp. (Os dados do jogo ficam em data/, so leitura.)
fs::path userDir() {
    const fs::path  here  = exeDir();
    const fs::path  probe = here / L".bluestinger_teste_escrita";
    std::error_code ec;
    {
        std::ofstream t(probe);
        if (t) {
            t.close();
            fs::remove(probe, ec);
            return here;
        }
    }
    const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
    fs::path       d     = local ? fs::path(local) / L"BlueStingerRecomp" : here;
    fs::create_directories(d, ec);
    return d;
}

// Sem console (subsistema grafico) e sem redirecionamento pelo processo pai: stdout e
// stderr vao para bluestinger.log.
void setupLog(const fs::path& dir) {
    const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) return;  // saida redirecionada (testes, scripts)
    // o arquivo e zerado uma vez e as duas streams abrem em modo de acrescimo: cada escrita
    // vai para o fim do arquivo, entao stdout e stderr nao se sobrescrevem (no subsistema
    // grafico as streams comecam sem descritor; as duas precisam ser reabertas)
    // Compartilhado (_SH_DENYNO): as variantes _s (_wfreopen_s) abrem sem compartilhar, entao
    // a segunda abertura (stdout) falhava, deixava a stream invalida e o primeiro printf
    // abortava o processo por parametro invalido. Tambem permite ler o log com o jogo aberto.
    const fs::path log = dir / L"bluestinger.log";
    FILE*          t   = _wfsopen(log.c_str(), L"w", _SH_DENYNO);  // zera o arquivo
    if (!t) return;
    std::fclose(t);
    // _wfreopen (sem _s) reassocia as proprias streams e abre compartilhado, como fopen; no
    // subsistema grafico stdout/stderr comecam sem descritor, entao reabrir e o caminho
#pragma warning(suppress : 4996)
    const bool errOk = _wfreopen(log.c_str(), L"a", stderr) != nullptr;
#pragma warning(suppress : 4996)
    const bool outOk = _wfreopen(log.c_str(), L"a", stdout) != nullptr;
    if (errOk) std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (outOk) std::setvbuf(stdout, nullptr, _IONBF, 0);
}

// Falhas que encerram o processo sem mensagem (parametro invalido do CRT, std::terminate)
// ficam registradas no log antes de abortar.
void setupCrashLog() {
    _set_invalid_parameter_handler([](const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned line,
                                      uintptr_t) {
        std::fprintf(stderr, "ERRO FATAL: parametro invalido no CRT: %s em %s (%s:%u)\n",
                     narrow(expr ? expr : L"?").c_str(), narrow(func ? func : L"?").c_str(),
                     narrow(file ? file : L"?").c_str(), line);
        std::fflush(stderr);
        std::abort();
    });
    std::set_terminate([] {
        std::fprintf(stderr, "ERRO FATAL: std::terminate (excecao nao tratada)\n");
        std::fflush(stderr);
        std::abort();
    });
}

std::string readFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("nao foi possivel abrir " + utf8(path));
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// BMP de 24 bits (de baixo para cima), sem dependencias.
bool writeBmp(const fs::path& path, const bs::GameFrame& f) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const uint32_t rowBytes = (f.width * 3 + 3) & ~3u, dataSize = rowBytes * f.height;
    auto           u32      = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto           u16      = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("BM", 2);
    u32(54 + dataSize), u32(0), u32(54);
    u32(40), u32(f.width), u32(f.height), u16(1), u16(24), u32(0), u32(dataSize), u32(2835), u32(2835), u32(0), u32(0);
    std::vector<uint8_t> row(rowBytes, 0);
    for (uint32_t y = f.height; y-- > 0;) {
        for (uint32_t x = 0; x < f.width; ++x) {
            const uint32_t p = f.argb[size_t(y) * f.width + x];
            row[3 * x] = uint8_t(p), row[3 * x + 1] = uint8_t(p >> 8), row[3 * x + 2] = uint8_t(p >> 16);
        }
        out.write(reinterpret_cast<const char*>(row.data()), rowBytes);
    }
    return bool(out);
}

// Nome neutro (o do mapa de teclado de runtime/input) de uma tecla do SDL; vazio se nao mapeavel.
std::string keyName(SDL_Keycode k) {
    if (k >= SDLK_A && k <= SDLK_Z) return std::string(1, char('A' + (k - SDLK_A)));
    if (k >= SDLK_0 && k <= SDLK_9) return std::string(1, char('0' + (k - SDLK_0)));
    if (k >= SDLK_F1 && k <= SDLK_F12) return "F" + std::to_string(1 + (k - SDLK_F1));
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return "ENTER";
    case SDLK_SPACE: return "SPACE";
    case SDLK_ESCAPE: return "ESC";
    case SDLK_TAB: return "TAB";
    case SDLK_BACKSPACE: return "BACKSPACE";
    case SDLK_LSHIFT: case SDLK_RSHIFT: return "SHIFT";
    case SDLK_LCTRL: case SDLK_RCTRL: return "CTRL";
    case SDLK_UP: return "UP";
    case SDLK_DOWN: return "DOWN";
    case SDLK_LEFT: return "LEFT";
    case SDLK_RIGHT: return "RIGHT";
    default: return "";
    }
}

void printStats(const bs::GameHost& host) {
    const bs::GameStats s = host.stats();
    std::printf("desempenho: %llu VBlanks; trabalho por campo: media %.2f ms, maximo %.2f ms; acima de 16,68 ms: "
                "%llu; ultimo ritmo %.2f campos/s\n",
                (unsigned long long)s.vblanks, s.workAvgMs, s.workMaxMs, (unsigned long long)s.overBudget,
                s.fieldsPerSecond);
}

void errorBox(SDL_Window* parent, const std::string& msg) {
    std::fprintf(stderr, "erro: %s\n", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, msg.c_str(), parent);
}

// Dados do jogo validos = manifesto e arquivos presentes, executavel de boot com o hash da
// versao recompilada. A mensagem diz o que falta e como gerar de novo.
bool validateData(const fs::path& dir, bs::FilesProvider files, std::string& err) {
    try {
        bs::boot_read_data(dir);
        const fs::path mods = exeDir() / L"mods" / L"assets";
        if (files == bs::FilesProvider::Native) {
            bs::native::files::DataFileSystem check(dir / L"disc", mods);  // mods de recursos validos?
            bs::transitional::gdfs::check_sizes(check);                    // cabem no contrato do jogo?
        }
        else
            bs::disc::RuntimeDisc check(dir, mods);  // legado: mods de arquivo cabem nos setores?
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// Capturas do headless (--dump-every N --capture <dir>), na thread do jogo, nos VBlanks multiplos de N (exatos: a mesma
// rota em duas execucoes captura os mesmos quadros). BS_CAPTURA_VBLANKS=a-b,c-d limita a essas faixas; BS_CAPTURA_CRUA=1
// grava tambem o quadro sem a legenda (vNNNNNN_crua.bmp) quando ha legenda; com a resolucao interna, vNNNNNN_hd.bmp.
std::function<void(const bs::GameFrame&)> headlessCapture(const Args& a) {
    std::error_code ec;
    fs::create_directories(a.capture, ec);
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    if (const char* r = std::getenv("BS_CAPTURA_VBLANKS"))
        for (const char* q = r; *q;) {
            unsigned long long x = 0, y = 0;
            int                n = 0;
            if (std::sscanf(q, "%llu-%llu%n", &x, &y, &n) != 2) break;
            ranges.push_back({x, y}), q += n;
            if (*q == ',') ++q;
        }
    const bool raw  = std::getenv("BS_CAPTURA_CRUA") != nullptr;
    auto       subs = std::make_shared<bs::SubtitleOverlay>();  // N7c: a captura mostra a legenda como o jogador a ve
    return [dir = a.capture, ranges, raw, subs](const bs::GameFrame& f) {
        const uint64_t v = f.vblank;
        bool           want = ranges.empty();
        for (const auto& [x, y] : ranges) want = want || (v >= x && v <= y);
        if (!want || !f.width) return;
        char name[40];
        std::snprintf(name, sizeof name, "v%06llu.bmp", (unsigned long long)v);
        writeBmp(dir / name, subs->composed(f));
        if (raw && !f.subtitle.empty()) {
            std::snprintf(name, sizeof name, "v%06llu_crua.bmp", (unsigned long long)v);
            writeBmp(dir / name, f);
        }
        if (f.hires) {  // N7c: a imagem na resolucao interna (sem a legenda), as linhas exibidas
            bs::GameFrame hd;
            hd.argb.assign(f.hires->begin() + size_t(f.hiresY0) * f.hiresWidth,
                           f.hires->begin() + size_t(f.hiresY0 + f.hiresRows) * f.hiresWidth);
            hd.width = f.hiresWidth, hd.height = f.hiresRows;
            std::snprintf(name, sizeof name, "v%06llu_hd.bmp", (unsigned long long)v);
            writeBmp(dir / name, hd);
        }
    };
}

int runHeadless(bs::GameHost& host, const Args& a) {
    // as capturas (--dump-every N --capture <dir>) sao feitas na thread do jogo (headlessCapture)
    host.join();
    bs::GameFrame f;
    uint64_t      seq = 0;
    const bool    got = host.latestFrame(f, seq);
    std::printf("VBlanks: %llu; quadro %ux%u\n", (unsigned long long)host.vblanks(), f.width, f.height);
    printStats(host);
    if (!host.error().empty()) {
        std::fprintf(stderr, "erro: %s\n", host.error().c_str());
        return 1;
    }
    if (!a.dump.empty() && (!got || !writeBmp(a.dump, f))) {
        std::fprintf(stderr, "nao foi possivel gravar %s\n", utf8(a.dump).c_str());
        return 1;
    }
    return 0;
}

void gamepadEvent(ni::InputSystem& sys, const SDL_Event& e);

// Diagnostico BS_CONTROLE_VIRTUAL=<roteiro> (N3-1, so desenvolvimento): um controle virtual do SDL3
// percorre o caminho real dos controles (eventos do SDL -> InputSystem -> ponte -> Maple) sem
// hardware. Linhas "<ms desde a abertura> <comando> [valor]"; comandos: conectar, desconectar, sair
// e os nomes do SDL de botoes (valor 0/1) e eixos (-32768..32767; gatilhos 0..32767).
class VirtualPad {
public:
    explicit VirtualPad(const std::string& text) {
        std::istringstream in(text);
        std::string        line;
        while (std::getline(in, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream ls(line);
            Ev                 ev;
            if (!(ls >> ev.ms >> ev.what)) continue;
            ls >> ev.value;
            evs_.push_back(ev);
        }
        t0_ = SDL_GetTicks();
        // o teste roda sem foco garantido: sem isto o SDL descarta os eventos do controle em segundo plano
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
    ~VirtualPad() { disconnect(); }

    // Aplica o que venceu; true = pedido de sair.
    bool step() {
        bool quit = false;
        for (; next_ < evs_.size() && SDL_GetTicks() - t0_ >= evs_[next_].ms; ++next_) {
            const Ev& e = evs_[next_];
            std::printf("[controle virtual] %llu ms: %s %d\n", static_cast<unsigned long long>(e.ms), e.what.c_str(), e.value);
            if (e.what == "conectar") connect();
            else if (e.what == "desconectar") disconnect();
            else if (e.what == "sair") quit = true;
            else if (!pad_) continue;
            else if (const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(e.what.c_str()); b != SDL_GAMEPAD_BUTTON_INVALID)
                SDL_SetJoystickVirtualButton(pad_, b, e.value != 0);
            else if (const SDL_GamepadAxis x = SDL_GetGamepadAxisFromString(e.what.c_str()); x != SDL_GAMEPAD_AXIS_INVALID) {
                const bool trig = x == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || x == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                // gatilho virtual: bruto -32768..32767 vira 0..32767 no gamepad
                SDL_SetJoystickVirtualAxis(pad_, x, Sint16(std::clamp(trig ? 2 * e.value - 32768 : e.value, -32768, 32767)));
            }
        }
        return quit;
    }

private:
    struct Ev {
        uint64_t    ms = 0;
        std::string what;
        int         value = 0;
    };
    void connect() {
        if (pad_) return;
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type     = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes    = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.name     = "Controle virtual";
        id_        = SDL_AttachVirtualJoystick(&d);
        pad_       = id_ ? SDL_OpenJoystick(id_) : nullptr;
        if (!pad_) std::fprintf(stderr, "[controle virtual] falhou: %s\n", SDL_GetError());
    }
    void disconnect() {
        if (pad_) SDL_CloseJoystick(pad_), pad_ = nullptr;
        if (id_) SDL_DetachVirtualJoystick(id_), id_ = 0;
    }
    std::vector<Ev> evs_;
    size_t          next_ = 0;
    uint64_t        t0_   = 0;
    SDL_JoystickID  id_   = 0;
    SDL_Joystick*   pad_  = nullptr;
};

// Modo espectador (desenvolvimento): BS_INPUT_SCRIPT com janela. O roteiro e a unica entrada (teclado e
// controles nao chegam ao jogo) e o titulo mostra passo de jogo, sala e botoes. O estado e lido no gancho
// de VBlank (passo, PDS da porta A na RAM) e no registro de arquivos (ultimo R*.BIN), sem mudar o jogo.
struct Spectator {
    std::atomic<bool>     on{false};
    std::atomic<uint64_t> step{0};
    std::atomic<uint32_t> buttons{0};  // `on` do PDS da porta A (bits do SDK pd)
};
Spectator g_spectator;

std::string spectatorText() {
    static const std::pair<uint32_t, const char*> kBits[] = {
        {0x8, "START"}, {0x10, "UP"}, {0x20, "DOWN"}, {0x40, "LEFT"}, {0x80, "RIGHT"}, {0x4, "A"},
        {0x2, "B"},     {0x400, "X"}, {0x200, "Y"},   {0x20000, "LT"}, {0x10000, "RT"}};
    std::string    pad;
    const uint32_t on = g_spectator.buttons.load();
    for (const auto& [bit, name] : kBits)
        if (on & bit) pad += (pad.empty() ? "" : "+") + std::string(name);
    const std::string room = bs::file_log_last_room();
    char              t[160];
    std::snprintf(t, sizeof t, " | ESPECTADOR passo %llu, sala %s, pad %s", static_cast<unsigned long long>(g_spectator.step.load()),
                  room.empty() ? "-" : room.c_str(), pad.empty() ? "-" : pad.c_str());
    return t;
}

// Gravacao de sessao (desenvolvimento, tools/dev/gravacoes.py): BS_GRAVAR=<arquivo> na janela grava o que o
// jogador faz com teclado ou controle como um roteiro "indice passo" (o formato do BS_INPUT_SCRIPT), uma linha a
// cada mudanca. A amostra fica congelada dentro do passo de jogo e passa pelos bytes do controle do Dreamcast, entao
// o jogo ve na gravacao exatamente o que vera na reproducao. BS_GRAVAR_BASE=<roteiro> com BS_GRAVAR_ATE=<passo>:
// antes desse passo vale o roteiro (copiado para a gravacao) e o jogador nao controla (continuar de um checkpoint).
// F5 marca um checkpoint (comentario com o passo e o quadro em <pasta do arquivo>/checkpoint_<n>.bmp; o quadro e o mais
// recente no fim da iteracao da janela, entao pode ser de um VBlank antes do passo anotado: so ilustra). A logica da
// gravacao fica em transitional/input_bridge.h (SessionRecorder, com testes); aqui so o arquivo e os checkpoints.
struct Recorder {
    FILE*                                              f = nullptr;
    fs::path                                           dir;
    std::unique_ptr<ib::SessionRecorder>               rec;
    int                                                checkpoints = 0;
    bool                                               pendingCheckpoint = false;

    void checkpoint(uint64_t gameStep, const bs::GameFrame& frame) {
        ++checkpoints;
        rec->comment("checkpoint " + std::to_string(checkpoints) + " passo " + std::to_string(gameStep));
        if (frame.width) writeBmp(dir / ("checkpoint_" + std::to_string(checkpoints) + ".bmp"), frame);
        std::printf("[gravacao] checkpoint %d no passo %llu\n", checkpoints, static_cast<unsigned long long>(gameStep));
    }
};
std::unique_ptr<Recorder> g_recorder;

namespace st = bs::native::settings;

// A apresentacao da janela (Vulkan direta ou SDL); trocada pelo SDL se a do Vulkan falhar (o menu usa a atual).
struct PresenterRef {
    std::unique_ptr<bs::WindowPresenter> p;
};

// N7c: o menu interno na janela (docs/N7-PLANO.md secao 19). O estado e a navegacao sao de app/settings_menu, o desenho
// de app/menu_overlay; aqui so a ligacao com o SDL e com o jogo:
//   - abre com ESC ou com o botao View/Back do controle (o jogo nao os usa no mapa padrao); fecha com eles ou com
//     Voltar na pagina principal. Aberto, o jogo fica PAUSADO entre VBlanks (GameHost::setPaused) e a entrada vai so
//     para o menu: teclas e botoes pressionados nao chegam ao jogo; os soltos, sim (nada fica preso ao voltar);
//   - teclado (setas, Enter/Espaco, Esc/Backspace), controle (D-pad e analogico com repeticao, A, B, LB/RB) e mouse
//     (passar por cima seleciona, clique na linha escolhe, nas setas muda, botao direito volta, roda rola);
//   - cada mudanca vale na hora quando e segura (volumes, legendas, renderizador, V-Sync, modo e tamanho da janela,
//     estes com a confirmacao de 15 s do modelo) e vai para graficos.toml, audio.toml e legendas.toml (gravacao
//     atomica) na pasta do usuario.
class InGameMenu {
public:
    // `prefs`: a configuracao efetiva (com as opcoes da linha de comando); `persisted`: a dos arquivos. Uma opcao da
    // linha de comando (`renderOverride`, `subtitleOverride`) vale so nesta execucao e nunca vai para o arquivo, a nao
    // ser que o jogador mude esse item no menu (revisao Codex 2026-10-22).
    InGameMenu(bs::GameHost& host, ni::InputSystem& input, SDL_Window* window, PresenterRef& pres, fs::path dir,
               bs::Preferences prefs, bs::Preferences persisted, bool renderOverride, bool subtitleOverride, bool cameraOverride,
               st::Capabilities caps, std::vector<std::string> languages)
        : host_(host), input_(input), window_(window), pres_(pres), dir_(std::move(dir)),
          menu_(prefs, caps, std::move(languages)), applied_(prefs), persisted_(std::move(persisted)),
          renderOverride_(renderOverride), subtitleOverride_(subtitleOverride), cameraOverride_(cameraOverride),
          confirmed_(prefs.graphics) {
        std::vector<bs::MenuItem> info;
        for (const char* action : {"A", "B", "X", "Y", "START", "UP", "DOWN", "LEFT", "RIGHT", "LT", "RT"}) {
            std::string keys, pads;
            for (const auto& b : input.map().keyboard)
                if (targetLabel(b.target) == action) keys += (keys.empty() ? "" : ", ") + b.control;
            for (const auto& b : input.map().gamepad)
                if (targetLabel(b.target) == action) pads += (pads.empty() ? "" : ", ") + b.control;
            info.push_back({action, (keys.empty() ? "-" : keys) + "  /  " + (pads.empty() ? "-" : pads), true});
        }
        menu_.set_controls_info(std::move(info));
    }

    bool open() const { return menu_.is_open(); }
    const bs::Preferences& prefs() const { return applied_; }

    // true = o evento foi do menu (nao vai ao jogo)
    bool event(const SDL_Event& e) {
        if (!enabled_) return false;
        const double now = SDL_GetTicks() / 1000.0;
        const bool   down = e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        // abrir/fechar
        if ((e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && !e.key.repeat && !menu_.is_open()) ||
            (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
            if (menu_.is_open()) close();
            else openMenu();
            return true;
        }
        if (!menu_.is_open()) return false;
        switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
            case SDLK_UP: act(bs::MenuAction::Up, now); break;
            case SDLK_DOWN: act(bs::MenuAction::Down, now); break;
            case SDLK_LEFT: act(bs::MenuAction::Left, now); break;
            case SDLK_RIGHT: act(bs::MenuAction::Right, now); break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE: act(bs::MenuAction::Confirm, now); break;
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE: act(bs::MenuAction::Back, now); break;
            default: break;
            }
            return true;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            switch (e.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP: act(bs::MenuAction::Up, now), hold(bs::MenuAction::Up, now); break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: act(bs::MenuAction::Down, now), hold(bs::MenuAction::Down, now); break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: act(bs::MenuAction::Left, now), hold(bs::MenuAction::Left, now); break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: act(bs::MenuAction::Right, now), hold(bs::MenuAction::Right, now); break;
            case SDL_GAMEPAD_BUTTON_SOUTH: act(bs::MenuAction::Confirm, now); break;
            case SDL_GAMEPAD_BUTTON_EAST: act(bs::MenuAction::Back, now); break;
            default: break;
            }
            return true;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            held_ = -1;
            return false;  // soltar sempre chega ao jogo (nada preso ao voltar)
        case SDL_EVENT_KEY_UP: return false;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                const float v = float(e.gaxis.value) / 32767.0f;
                const bool  y = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY;
                stick_[y ? 1 : 0] = v;
                const int dir = std::fabs(v) > 0.6f ? (v > 0 ? 1 : -1) : 0;
                const bs::MenuAction a = y ? (dir > 0 ? bs::MenuAction::Down : bs::MenuAction::Up)
                                           : (dir > 0 ? bs::MenuAction::Right : bs::MenuAction::Left);
                if (dir && held_ != int(a)) act(a, now), hold(a, now);
                if (!dir && std::fabs(stick_[0]) < 0.6f && std::fabs(stick_[1]) < 0.6f) held_ = -1;
            }
            return false;  // o eixo vai ao jogo (pausado; ao voltar ele le o estado atual)
        case SDL_EVENT_MOUSE_MOTION:
            if (const auto h = overlay_.hit(px(e.motion.x), py(e.motion.y))) menu_.select(h->item);
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_RIGHT) act(bs::MenuAction::Back, now);
            else if (e.button.button == SDL_BUTTON_LEFT) {
                if (!menu_.view().prompt.empty()) act(bs::MenuAction::Confirm, now);  // pergunta aberta: clique confirma
                else if (const auto h = overlay_.hit(px(e.button.x), py(e.button.y))) {
                    if (h->part == bs::MenuHit::Part::Decrease) menu_.adjust(h->item, -1, now);
                    else if (h->part == bs::MenuHit::Part::Increase) menu_.adjust(h->item, +1, now);
                    else menu_.select(h->item), act(bs::MenuAction::Confirm, now);
                }
            }
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            act(e.wheel.y > 0 ? bs::MenuAction::Up : bs::MenuAction::Down, now);
            return true;
        default: return down;
        }
    }

    // A cada volta do laco da janela: repeticao do controle, reversao da confirmacao, aplicacao e gravacao.
    void update() {
        const double now = SDL_GetTicks() / 1000.0;
        if (menu_.is_open()) {
            menu_.tick(now);
            if (held_ >= 0 && now >= nextRepeat_) act(bs::MenuAction(held_), now), nextRepeat_ = now + 0.12;
            if (!menu_.is_open()) close();  // "Voltar ao jogo" ou Voltar na pagina principal
        }
        bs::Preferences next;
        if (menu_.take_changes(next)) apply(next, false);
        // a tela so vai para o arquivo depois de aceita: sem confirmacao em curso, a aplicada e a confirmada
        if (!menu_.display_pending() && !sameDisplay(confirmed_, applied_.graphics)) {
            confirmed_ = applied_.graphics;
            persist();
        }
    }

    // As camadas nativas (legendas e menu) por cima do quadro, em pixels da saida w x h, posicionadas na area do jogo
    // (texto nitido, sem distorcer). capture = a apresentacao deste quadro deve ser capturada (BS_MENU_CAPTURA).
    void layers(bs::SubtitleOverlay& subs, const std::wstring& subtitle, int w, int h, const SDL_FRect& area,
                std::vector<bs::WindowLayer>& out, bool& capture) {
        capture = false;
        out.assign(2, bs::WindowLayer{});  // indices fixos: 0 legendas, 1 menu
        if (w <= 0 || h <= 0) return;
        if (styleDirty_) subs.set_style(style_), styleDirty_ = false;
        if (const auto* l = subs.layer(subtitle, uint32_t(w), uint32_t(h), area.x, area.y, area.w, area.h))
            out[0] = {l, uint32_t(w), uint32_t(h), subs.version()};
        if (menu_.is_open()) {
            const bs::MenuView v = menu_.view();
            const auto&        l = overlay_.layer(v, uint32_t(w), uint32_t(h), area.x, area.y, area.w, area.h);
            out[1] = {&l, uint32_t(w), uint32_t(h), overlay_.version()};
            // diagnostico: BS_MENU_CAPTURA=<pasta> grava a imagem APRESENTADA (jogo + legendas + menu) a cada mudanca do menu
            static const char* capDir = std::getenv("BS_MENU_CAPTURA");
            const std::string  k      = v.title + "|" + std::to_string(v.selected) + "|" + v.prompt +
                                  (v.selected >= 0 && v.selected < int(v.items.size()) ? v.items[size_t(v.selected)].value : "");
            if (capDir && *capDir && k != lastCapture_) lastCapture_ = k, capture = true;
        }
        outW_ = w, outH_ = h;
    }
    // o shader de mod escolhido falhou depois de aplicado (perda do dispositivo, recarga recusada): desativado e gravado
    void shader_failed(const std::string& why) {
        const std::string name = applied_.graphics.shader;
        std::fprintf(stderr, "[menu] shader %s desativado: %s\n", name.empty() ? "?" : name.c_str(), why.c_str());
        if (!name.empty()) menu_.mark_shader_invalid(name, why);
    }

    void captured(const std::vector<uint32_t>& img, int w, int h) {
        static const char* capDir = std::getenv("BS_MENU_CAPTURA");
        if (!capDir || !*capDir || w <= 0 || h <= 0 || img.size() < size_t(w) * h) return;
        bs::GameFrame f;
        f.argb = img, f.width = uint32_t(w), f.height = uint32_t(h);
        char name[48];
        std::snprintf(name, sizeof name, "menu_%03d.bmp", ++captures_);
        std::error_code ec;
        fs::create_directories(capDir, ec);
        writeBmp(fs::path(capDir) / name, f);
    }

    // As preferencias que ja valem na partida (no inicio e a cada mudanca do menu); `start` = so aplica, sem gravar.
    void apply(const bs::Preferences& p, bool start) {
        const bs::Preferences old = applied_;
        applied_                  = p;
        // legendas: estilo na camada; idioma (ou desligadas) no jogo (falas e filmes)
        bs::native::video::OverlayStyle s;
        s.scale = float(p.subtitles.size_pct) / 100.0f, s.outline = p.subtitles.outline, s.box = p.subtitles.box;
        style_ = s, styleDirty_ = true;
        if (start || p.subtitles.effective_language() != old.subtitles.effective_language())
            host_.setSubtitleLanguage(p.subtitles.effective_language());
        // audio (nativo): os tres barramentos com o volume geral
        if (start || !(p.audio == old.audio)) host_.setVolumes(p.audio.gain_music(), p.audio.gain_voices(), p.audio.gain_effects());
        // graficos que ja existem: renderizador, V-Sync, modo e tamanho da janela
        const auto& g = p.graphics;
        if (!start && g.vulkan != old.graphics.vulkan)
            host_.setRenderer(g.vulkan ? bs::RenderProvider::Native : bs::RenderProvider::Software);
        // N7c (secao 21): efeitos de imagem na apresentacao (so a imagem do jogo; desligados = nenhum passe)
        if (pres_.p) {
            bs::PostSpec ps;
            ps.color = g.color, ps.brightness = g.brightness, ps.contrast = g.contrast, ps.gamma = g.gamma, ps.saturation = g.saturation;
            ps.tonemap = g.tonemap, ps.exposure = g.exposure, ps.highlights = g.highlights, ps.toneShadows = g.tone_shadows;
            ps.sharpen = g.sharpen;
            ps.bloom = g.bloom, ps.bloomThreshold = g.bloom_threshold, ps.bloomRadius = g.bloom_radius, ps.bloomStrength = g.bloom_strength;
            ps.crt = g.crt && g.vulkan && st::internal_height(g) >= st::kCrtMinHeight;
            ps.crtScanlines = g.crt_scanlines, ps.crtMask = g.crt_mask, ps.crtVignette = g.crt_vignette;
            for (const auto& pkg : shaders_)
                if (pkg.error.empty() && pkg.name == g.shader) {
                    ps.shader = &pkg;
                    for (const auto& q : pkg.params) {
                        const auto it = g.shader_params.find(q.name);
                        ps.shaderParams.push_back(it != g.shader_params.end() ? it->second : q.def);
                    }
                }
            if (start || !(ps == post_)) {
                post_ = ps;
                if (const std::string why = pres_.p->set_post(ps); !why.empty()) {
                    std::fprintf(stderr, "[menu] shader %s desativado: %s\n", g.shader.c_str(), why.c_str());
                    menu_.mark_shader_invalid(g.shader, why);  // o proximo update aplica e grava sem ele
                }
            }
        }
        // resolucao interna (so o Vulkan; o software desenha na do jogo): altura interna / 480
        if (start || g.vulkan != old.graphics.vulkan || st::internal_height(g) != st::internal_height(old.graphics))
            host_.setRenderScale(g.vulkan ? float(st::internal_height(g)) / 480.f : 1.f);
        if ((start || g.vsync != old.graphics.vsync) && pres_.p) pres_.p->set_vsync(g.vsync);
        if (start || g.window != old.graphics.window || g.window_w != old.graphics.window_w || g.window_h != old.graphics.window_h) {
            if (!applyWindow(g)) {  // o SDL recusou: nunca fica sem imagem
                std::fprintf(stderr, "[menu] modo de tela recusado (%s): ", SDL_GetError());
                if (start) {  // o salvo nao serve: janela no tamanho padrao, e o arquivo fica como estava
                    std::fprintf(stderr, "janela\n");
                    applied_.graphics.window = st::WindowMode::Windowed, applied_.graphics.window_w = applied_.graphics.window_h = 0;
                    confirmed_ = applied_.graphics;
                    applyWindow(applied_.graphics);
                } else {
                    std::fprintf(stderr, "a configuracao anterior volta\n");
                    menu_.cancel_display();  // o proximo update aplica a anterior
                }
            }
        }
        // N7c: camera livre (o GameHost ja comecou com ela; a troca de modo instala ou desinstala na thread do jogo)
        if (!start && !(p.camera == old.camera)) host_.setCamera(p.camera);
        if (start) return;
        // um item da linha de comando mudado no menu deixa de ser so desta execucao
        if (g.vulkan != old.graphics.vulkan) renderOverride_ = false;
        if (p.camera.mode != old.camera.mode) cameraOverride_ = false;
        if (p.subtitles.enabled != old.subtitles.enabled || p.subtitles.language != old.subtitles.language) subtitleOverride_ = false;
        persist();
    }

    const bs::native::video::OverlayStyle& subtitle_style() const { return style_; }

    // O que vai para os arquivos: a configuracao aplicada, menos o que ainda nao e escolha do jogador (a tela sem
    // confirmacao e as opcoes da linha de comando que ele nao mudou). So grava os grupos que mudaram (atomico).
    void persist() {
        bs::Preferences d = persisted_;
        d.audio           = applied_.audio;
        d.graphics        = applied_.graphics;
        d.graphics.window = confirmed_.window, d.graphics.window_w = confirmed_.window_w, d.graphics.window_h = confirmed_.window_h;
        d.graphics.resolution = confirmed_.resolution, d.graphics.scale_pct = confirmed_.scale_pct;
        if (renderOverride_) d.graphics.vulkan = persisted_.graphics.vulkan;
        d.subtitles = applied_.subtitles;
        if (subtitleOverride_) d.subtitles.enabled = persisted_.subtitles.enabled, d.subtitles.language = persisted_.subtitles.language;
        d.camera = applied_.camera;
        if (cameraOverride_) d.camera.mode = persisted_.camera.mode;
        std::string err;
        bool        wrote = false;
        if (!(d.graphics == persisted_.graphics)) wrote = true, st::write_graphics(dir_, d.graphics, &err) || log(err);
        if (!(d.audio == persisted_.audio)) wrote = true, st::write_audio(dir_, d.audio, &err) || log(err);
        if (!(d.subtitles == persisted_.subtitles)) wrote = true, st::write_subtitles(dir_, d.subtitles, &err) || log(err);
        if (!(d.camera == persisted_.camera)) wrote = true, st::write_camera(dir_, d.camera, &err) || log(err);  // so a secao [camera]
        persisted_ = d;
        if (wrote) std::fprintf(stderr, "[menu] preferencias gravadas em %s\n", utf8(dir_).c_str());
    }

private:
    bs::GameHost&                     host_;
    ni::InputSystem&                  input_;
    SDL_Window*                       window_;
    PresenterRef&                     pres_;
    fs::path                          dir_;
    std::vector<bs::native::render::PostShaderPackage> shaders_;  // mods\shaders (validos e invalidos)
    bs::PostSpec                      post_;

public:
    void set_shaders(std::vector<bs::native::render::PostShaderPackage> pkgs) {
        shaders_ = std::move(pkgs);
        std::vector<bs::ShaderChoice> choices;
        for (const auto& p : shaders_) {
            bs::ShaderChoice c;
            c.name = p.name, c.description = p.description, c.error = p.error;
            for (const auto& q : p.params) c.params.push_back({q.name, q.label, q.min, q.max, q.def, q.step});
            choices.push_back(std::move(c));
        }
        menu_.set_shaders(std::move(choices));
    }

private:
    bs::SettingsMenu                  menu_;
    bs::MenuOverlay                   overlay_;
    bs::Preferences                   applied_, persisted_;
    bool                              renderOverride_ = false, subtitleOverride_ = false, cameraOverride_ = false;
    st::GraphicsSettings              confirmed_;  // a ultima tela aceita (a que vai para o arquivo)

    static bool sameDisplay(const st::GraphicsSettings& a, const st::GraphicsSettings& b) {
        return a.window == b.window && a.window_w == b.window_w && a.window_h == b.window_h && a.resolution == b.resolution &&
               a.scale_pct == b.scale_pct;
    }
    static bool log(const std::string& e) {
        std::fprintf(stderr, "[menu] %s\n", e.c_str());
        return true;
    }
    bs::native::video::OverlayStyle   style_;
    bool                              styleDirty_ = true;
    bool                              enabled_    = true;

public:
    void disable() { enabled_ = false; }  // modo espectador: o roteiro controla, sem menu

private:
    int                               outW_ = 0, outH_ = 0;
    int                               held_ = -1, captures_ = 0;
    std::string                       lastCapture_;
    double                            nextRepeat_ = 0;
    float                             stick_[2] = {};

    static std::string targetLabel(const ni::Target& t) {
        switch (t.kind) {
        case ni::Target::Kind::Button: return ni::button_name(ni::Button(t.button));
        case ni::Target::Kind::AxisDirection:
            if (t.axis == ni::Axis::TriggerL) return "LT";
            if (t.axis == ni::Axis::TriggerR) return "RT";
            return "ANALOGICO";
        default: return "ANALOGICO";
        }
    }

    // posicao do mouse (coordenadas da janela) em pixels de saida
    float px(float x) const {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window_, &ww, &wh);
        return ww > 0 ? x * float(outW_) / float(ww) : x;
    }
    float py(float y) const {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window_, &ww, &wh);
        return wh > 0 ? y * float(outH_) / float(wh) : y;
    }

    void act(bs::MenuAction a, double now) { menu_.input(a, now); }
    void hold(bs::MenuAction a, double now) { held_ = int(a), nextRepeat_ = now + 0.4; }

    void openMenu() {
        input_.release_keyboard();  // teclas do jogo seguras ate aqui soltam (o jogo fica pausado)
        menu_.open();
        host_.setPaused(true);
        std::fprintf(stderr, "[menu] aberto (jogo pausado no VBlank %llu)\n", static_cast<unsigned long long>(host_.vblanks()));
    }
    void close() {
        if (menu_.is_open()) menu_.close();
        bs::Preferences next;
        if (menu_.take_changes(next)) apply(next, false);  // uma confirmacao de tela pendente voltou ao fechar
        held_ = -1;
        host_.setPaused(false);
        std::fprintf(stderr, "[menu] fechado no VBlank %llu (jogo continua)\n", static_cast<unsigned long long>(host_.vblanks()));
    }

    // false = o SDL recusou alguma parte (quem chama desfaz); SDL_SyncWindow espera a mudanca assincrona terminar
    bool applyWindow(const st::GraphicsSettings& g) {
        bool ok = true;
        if (g.window == st::WindowMode::Windowed) {
            ok = SDL_SetWindowFullscreen(window_, false);
            if (ok && g.window_w && g.window_h) ok = SDL_SetWindowSize(window_, int(g.window_w), int(g.window_h));
        } else if (g.window == st::WindowMode::Borderless) {
            ok = SDL_SetWindowFullscreenMode(window_, nullptr) && SDL_SetWindowFullscreen(window_, true);  // area de trabalho
        } else {
            const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_));
            ok = dm && SDL_SetWindowFullscreenMode(window_, dm) && SDL_SetWindowFullscreen(window_, true);  // exclusiva
        }
        if (ok) ok = SDL_SyncWindow(window_);
        return ok;
    }
};

// A apresentacao Vulkan falhou de vez: a janela continua com o SDL_Renderer (nunca sem imagem).
bool fallbackToSdl(SDL_Window* window, PresenterRef& pres, SDL_Renderer*& renderer) {
    std::fprintf(stderr, "[apresentacao] a apresentacao Vulkan falhou: recuando para o SDL\n");
    pres.p.reset();
    if (!renderer) renderer = SDL_CreateRenderer(window, nullptr);
    pres.p = bs::make_sdl_presenter(window, renderer);
    if (pres.p) pres.p->set_vsync(true);
    return pres.p != nullptr;
}

int runWindow(bs::GameHost& host, ni::InputSystem& input, const Args& a, SDL_Window* window, PresenterRef& pres,
              SDL_Renderer*& renderer, InGameMenu& menu) {
    // N7c: legendas das falas por cima do gameplay, na resolucao da janela (camada propria; o quadro nao e tocado)
    bs::SubtitleOverlay subs;
    bs::GameFrame frame;
    uint64_t      seq  = 0;
    bool          quit = false, fullscreen = false;
    uint64_t      lastTitle = SDL_GetTicks();
    int           captures  = 0;
    int           gfxFailures = 0;
    uint64_t      lastWinShot = 0;  // BS_JANELA_CAPTURA
    std::string   gfxError;
    std::unique_ptr<VirtualPad> virtualPad;
    if (const char* v = std::getenv("BS_CONTROLE_VIRTUAL"); v && *v) virtualPad = std::make_unique<VirtualPad>(readFile(v));
    while (!quit) {
        if (virtualPad && virtualPad->step()) quit = true;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type != SDL_EVENT_QUIT && menu.event(e)) continue;  // N7c: o menu interno (aberto, a entrada e dele)
            switch (e.type) {
            case SDL_EVENT_QUIT:
                if (std::getenv("BS_APP_TRACE")) std::fprintf(stderr, "[janela] pedido de fechar\n");
                quit = true;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                if (e.key.key == SDLK_F12 && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !a.capture.empty() &&
                    frame.width) {  // captura do quadro exibido
                    const fs::path path = a.capture / ("quadro_" + std::to_string(++captures) + ".bmp");
                    std::printf("captura %s: %s\n", utf8(path).c_str(), writeBmp(path, subs.composed(frame)) ? "ok" : "FALHOU");
                    break;
                }
                if (e.key.key == SDLK_F5 && g_recorder) {
                    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) g_recorder->pendingCheckpoint = true;  // depois do quadro
                    break;
                }
                if (e.key.key == SDLK_F11 && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen);
                    break;
                }
                const std::string n = keyName(e.key.key);
                if (!n.empty()) input.key(n, e.type == SDL_EVENT_KEY_DOWN);
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_LOST: input.release_keyboard(); break;
            default: gamepadEvent(input, e); break;
            }
        }
        for (const ni::ConnectionEvent& ev : input.drain_events()) {
            if (ev.kind == ni::ConnectionEvent::Kind::Disconnected) std::printf("[controles] desconectado: %s\n", ev.name.c_str());
            else  // o GUID e o que o usuario copia para um [perfis."<guid>"]
                std::printf("[controles] conectado: %s (GUID %s; %s)\n", ev.name.c_str(), ev.guid.empty() ? "-" : ev.guid.c_str(),
                            ev.profile.empty() ? "mapa [controle]" : ("perfil " + ev.profile).c_str());
        }
        // falhas de apresentacao viram erro (janela preta com codigo 0 pareceria sucesso):
        // criar a textura falha de vez; as demais so se persistirem (~2 s seguidos)
        bool ok = true;
        host.latestFrame(frame, seq);
        if (g_recorder && g_recorder->pendingCheckpoint)  // F5: com o quadro desta iteracao
            g_recorder->pendingCheckpoint = false, g_recorder->checkpoint(g_spectator.step.load(), frame);
        // N7c: o quadro na area 4:3 da saida e as camadas nativas (legendas, menu) por cima, pela apresentacao atual
        menu.update();
        int ow = 0, oh = 0;
        pres.p->output_size(ow, oh);
        const SDL_FRect              area = bs::game_area(ow, oh);
        std::vector<bs::WindowLayer> layers;
        bool                         shot = false;
        menu.layers(subs, frame.subtitle, ow, oh, area, layers, shot);
        // diagnostico: BS_JANELA_CAPTURA=<pasta> grava a imagem APRESENTADA a cada BS_JANELA_CAPTURA_CADA quadros novos do
        // jogo (padrao 300): o que o jogador ve, com legendas e menu, no tamanho real da janela; BS_JANELA_CAPTURA_VBLANKS=a-b
        // limita a essa faixa
        static const char*     winDir   = std::getenv("BS_JANELA_CAPTURA");
        static const uint64_t  winEvery = [] {
            const char* v = std::getenv("BS_JANELA_CAPTURA_CADA");
            return v && *v ? std::max<uint64_t>(1, std::strtoull(v, nullptr, 10)) : 300ull;
        }();
        static const std::pair<uint64_t, uint64_t> winRange = [] {
            unsigned long long x = 0, y = ~0ull;
            if (const char* v = std::getenv("BS_JANELA_CAPTURA_VBLANKS")) std::sscanf(v, "%llu-%llu", &x, &y);
            return std::pair<uint64_t, uint64_t>{x, y};
        }();
        const bool winShot = winDir && *winDir && frame.vblank && frame.vblank / winEvery != lastWinShot &&
                             frame.vblank >= winRange.first && frame.vblank <= winRange.second;
        std::vector<uint32_t> image;
        ok &= pres.p->present(frame, seq, area, layers, shot || winShot ? &image : nullptr);
        if (shot && !image.empty()) menu.captured(image, ow, oh);
        if (const std::string why = pres.p->take_post_error(); !why.empty()) menu.shader_failed(why);
        if (winShot && !image.empty() && image.size() >= size_t(ow) * oh) {
            lastWinShot = frame.vblank / winEvery;
            bs::GameFrame f;
            f.argb = std::move(image), f.width = uint32_t(ow), f.height = uint32_t(oh);
            std::error_code ec;
            fs::create_directories(winDir, ec);
            char name[64];
            std::snprintf(name, sizeof name, "janela_v%06llu_%dx%d.bmp", (unsigned long long)frame.vblank, ow, oh);
            writeBmp(fs::path(winDir) / name, f);
        }
        gfxFailures = ok ? 0 : gfxFailures + 1;
        if (gfxFailures >= 120 && std::strcmp(pres.p->name(), "Vulkan") == 0 && fallbackToSdl(window, pres, renderer))
            gfxFailures = 0;
        if (gfxFailures >= 120) {
            gfxError = std::string("falha ao apresentar o quadro: ") + SDL_GetError();
            break;
        }
        if (!host.running()) quit = true;  // o jogo parou (erro): mostra a mensagem abaixo
        if (SDL_GetTicks() - lastTitle >= 1000) {  // ritmo do jogo no titulo da janela
            lastTitle             = SDL_GetTicks();
            const bs::GameStats s = host.stats();
            char                t[200];
            std::snprintf(t, sizeof t, "%s %s - %.1f campos/s, trabalho %.1f ms (max %.1f)", kTitle, BS_VERSION,
                          s.fieldsPerSecond, s.workAvgMs, s.workMaxMs);
            std::string full = g_spectator.on ? t + spectatorText() : std::string(t);
            if (g_recorder) {
                const uint64_t st = g_spectator.step.load();
                full += (st < g_recorder->rec->base_until() ? " | REPRODUZINDO ate o passo " + std::to_string(g_recorder->rec->base_until())
                                                    : std::string(" | GRAVANDO")) +
                        ", passo " + std::to_string(st) + " (F5 = checkpoint)";
            }
            SDL_SetWindowTitle(window, full.c_str());
        }
    }
    host.requestStop();
    host.join();
    printStats(host);
    if (pres.p) std::fprintf(stderr, "%s", pres.p->stats().c_str());
    const std::string err = host.error();
    if (!err.empty()) errorBox(window, "O jogo parou com erro:\n" + err);
    else if (!gfxError.empty()) errorBox(window, "Falha de video:\n" + gfxError);
    return err.empty() && gfxError.empty() ? 0 : 1;
}

// Mapa de acoes (N3-1): controles.toml na pasta do usuario, criado com o padrao na primeira
// execucao; --map (formato antigo "ACAO = TECLA") substitui o teclado. Arquivo invalido: aviso e
// mapa padrao (o jogo abre do mesmo jeito).
ni::ActionMap loadActionMap(const Args& a, std::string& warning) {
    const fs::path path = userDir() / L"controles.toml";
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        std::ofstream out(path, std::ios::binary);
        out << ni::ActionMap::default_text();
    }
    ni::ActionMap m = ni::ActionMap::defaults();
    std::string   err;
    if (fs::exists(path, ec)) {
        auto parsed = ni::ActionMap::parse(readFile(path), &err);
        if (parsed) m = parsed.value();
        else warning = "O arquivo de controles tem um erro e foi ignorado (valem os controles padrao):\n" +
                       utf8(path) + "\n" + err;
    }
    if (!a.map.empty() && !m.set_keyboard_legacy(readFile(a.map), &err))
        warning += (warning.empty() ? "" : "\n\n") + std::string("Mapa de teclado (--map) ignorado: ") + err;
    std::printf("[controles] mapa: %s%s\n", utf8(path).c_str(), a.map.empty() ? "" : " (teclado de --map)");
    return m;
}

// Entrada (N3-1): tudo passa pelo nucleo nativo (native/platform/input.h) e o Maple le dele pela
// ponte (transitional/input_bridge.h). Janela: teclado e controles (SDL3 Gamepad ou XInput).
// Testes: roteiro (BS_INPUT_SCRIPT) ou gameplay assistido como fonte exclusiva. Sem nada disso no
// headless, nenhuma fonte (testes diferenciais nao dependem do teclado de quem roda).
// Diagnostico BS_CONTROLE_FISICO=<roteiro> (N3-3, so headless): o roteiro (formato de sempre, indice por
// passo ou leitura) descreve CONTROLES FISICOS na posicao padrao (A = botao a, UP = dpup, AX = leftx,
// LT = lefttrigger...), aplicados a um controle simulado num InputSystem real com o mapa de
// BS_CONTROLES_TOML (padrao: o mapa padrao) e o GUID de BS_CONTROLE_GUID. Prova que o remapeamento e os
// perfis produzem no jogo o efeito das acoes equivalentes (test_pd_nativo P9).
ni::SampleFn physicalScript(const char* path) {
    ni::ActionMap m = ni::ActionMap::defaults();
    if (const char* toml = std::getenv("BS_CONTROLES_TOML"); toml && *toml) {
        std::string err;
        auto        parsed = ni::ActionMap::parse(readFile(toml), &err);
        if (!parsed) throw std::runtime_error("BS_CONTROLES_TOML: " + err);
        m = parsed.value();
    }
    auto              phys = std::make_shared<ni::InputSystem>(m);
    const std::string guidText(std::getenv("BS_CONTROLE_GUID") ? std::getenv("BS_CONTROLE_GUID") : "");
    const char*       guid = guidText.empty() ? nullptr : guidText.c_str();
    phys->gamepad_added(1, "controle do roteiro", guidText);
    auto plugged = std::make_shared<bool>(true);
    std::shared_ptr<bs::InputSource> src(bs::make_scripted_input(readFile(path)));
    std::fprintf(stderr, "[input] roteiro fisico %s (mapa %s, GUID %s)\n", path,
                 std::getenv("BS_CONTROLES_TOML") ? std::getenv("BS_CONTROLES_TOML") : "padrao", guid ? guid : "-");
    // DESCONECTADO tira o controle (ele solta tudo; o teclado continua, entao a porta do jogo fica
    // presente e neutra); o estado seguinte o reconecta com o mesmo GUID (o perfil volta a valer)
    return [phys, src, plugged, guidText](const ni::SampleContext& ctx) {
        const bs::PadState p = src->read(0, ctx.sample);
        if (!p.connected) {
            if (*plugged) phys->gamepad_removed(1), *plugged = false;
            return phys->latch(ctx);
        }
        if (!*plugged) phys->gamepad_added(1, "controle do roteiro", guidText), *plugged = true;
        if (p.buttons & (bs::kPadC | bs::kPadZ | bs::kPadD)) throw std::runtime_error("BS_CONTROLE_FISICO: C/Z/D nao tem botao fisico padrao");
        static const std::pair<uint16_t, const char*> kButtons[] = {
            {bs::kPadA, "a"},       {bs::kPadB, "b"},          {bs::kPadX, "x"},          {bs::kPadY, "y"},
            {bs::kPadStart, "start"}, {bs::kPadUp, "dpup"},    {bs::kPadDown, "dpdown"}, {bs::kPadLeft, "dpleft"},
            {bs::kPadRight, "dpright"}};
        for (const auto& [bit, name] : kButtons) phys->gamepad_button(1, name, (p.buttons & bit) != 0);
        phys->gamepad_axis(1, "leftx", ib::axis_from_byte(p.joyx));
        phys->gamepad_axis(1, "lefty", ib::axis_from_byte(p.joyy));
        phys->gamepad_axis(1, "rightx", ib::axis_from_byte(p.joy2x));
        phys->gamepad_axis(1, "righty", ib::axis_from_byte(p.joy2y));
        phys->gamepad_axis(1, "lefttrigger", ib::trigger_from_byte(p.ltrig));
        phys->gamepad_axis(1, "righttrigger", ib::trigger_from_byte(p.rtrig));
        return phys->latch(ctx);
    };
}

std::unique_ptr<ni::InputSystem> setupInput(const Args& a, bool window, std::string& warning,
                                            std::shared_ptr<bs::LiveScriptInput> live = nullptr) {
    std::unique_ptr<ni::InputSystem> sys;
    if (const char* fis = std::getenv("BS_CONTROLE_FISICO"); fis && *fis && !window && !live) {
        if (std::getenv("BS_INPUT_SCRIPT")) throw std::runtime_error("BS_CONTROLE_FISICO e BS_INPUT_SCRIPT sao exclusivos");
        sys = std::make_unique<ni::InputSystem>();
        sys->set_test_source(physicalScript(fis));
    } else if (live) {
        sys = std::make_unique<ni::InputSystem>();
        sys->set_test_source(ib::from_runtime_source(live));
    } else if (const char* script = std::getenv("BS_INPUT_SCRIPT")) {
        sys = std::make_unique<ni::InputSystem>();
        sys->set_test_source(ib::from_runtime_source(std::shared_ptr<bs::InputSource>(bs::make_scripted_input(readFile(script)))));
        std::fprintf(stderr, "[input] roteiro %s\n", script);
    } else if (window) {
        sys = std::make_unique<ni::InputSystem>(loadActionMap(a, warning));
        if (sys->map().options.backend == "xinput") sys->enable_xinput();
    } else if (const char* host = std::getenv("BS_INPUT"); host && std::string(host) == "host") {
        // headless com o teclado do Windows e XInput (diagnostico, como antes do N3-1)
        std::vector<std::unique_ptr<bs::InputSource>> v;
        const char* map = std::getenv("BS_INPUT_MAP");
        v.push_back(bs::make_keyboard_input(map ? readFile(map) : ""));
        v.push_back(bs::make_xinput_input());
        sys = std::make_unique<ni::InputSystem>();
        sys->set_test_source(ib::from_runtime_source(std::shared_ptr<bs::InputSource>(bs::make_combined_input(std::move(v)))));
        std::fprintf(stderr, "[input] teclado%s + XInput\n", map ? " (mapa do usuario)" : "");
    }
    // N3-2: no --controles nativo o pd nativo tira as amostras e o Maple nao tem controle nem
    // fonte (a ponte so fica no negativo BS_PAD_NATIVO_MUTACAO=maple). Sem fonte nenhuma, um
    // InputSystem vazio: controle presente e neutro, como o estado fixo do Maple no legado.
    if (a.controls == bs::ControlsProvider::Native) {
        if (!sys) sys = std::make_unique<ni::InputSystem>();
        const char* m = std::getenv("BS_PAD_NATIVO_MUTACAO");
        ib::install(m && std::strstr(m, "maple") ? sys.get() : nullptr);
        return sys;
    }
    ib::install(sys.get());
    return sys;
}

// Controles pelo SDL3 (N3-1): o SDL ja traz o banco de mapeamentos de Xbox, PlayStation, Switch e
// genericos; os nomes de botoes e eixos do SDL sao os do arquivo de controles.
void gamepadEvent(ni::InputSystem& sys, const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) {
            const char* n = SDL_GetGamepadName(g);
            char        guid[33] = {};  // N3-3: perfis por GUID ([perfis."<guid>"] no controles.toml)
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(e.gdevice.which), guid, sizeof guid);
            sys.gamepad_added(e.gdevice.which, n ? n : "controle", guid);
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        sys.gamepad_removed(e.gdevice.which);
        if (SDL_Gamepad* g = SDL_GetGamepadFromID(e.gdevice.which)) SDL_CloseGamepad(g);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (const char* n = SDL_GetGamepadStringForButton(SDL_GamepadButton(e.gbutton.button)))
            sys.gamepad_button(e.gbutton.which, n, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (const char* n = SDL_GetGamepadStringForAxis(SDL_GamepadAxis(e.gaxis.axis))) {
            const bool trig = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            sys.gamepad_axis(e.gaxis.which, n, trig ? ni::trigger_from_s16(e.gaxis.value) : ni::axis_from_s16(e.gaxis.value));
        }
        break;
    default: break;
    }
}

// Pasta dos saves nativos (N4, decisao do mantenedor 2026-10-19): Documentos\DC-Recomp\Blue Stinger\saves
// (FOLDERID_Documents, que segue o redirecionamento do Windows); --saves-pasta <dir> ou --saves-portatil
// (saves\ ao lado do executavel) escolhem outra. Sem a pasta Documentos, a pasta do usuario do log.
// instalacao.toml ao lado do executavel (gravado pelo BlueStinger-Recompiler, "Modo portatil") pede
// saves\ ali tambem, inclusive ao abrir o jogo direto e depois de mover a pasta.
// --headless (testes e diagnostico) sem --saves-pasta/--saves-portatil: cartao vazio e temporario do
// processo (apagado no fim), para que nenhum teste leia ou grave os saves do usuario.
struct TempSaves {
    fs::path dir;
    ~TempSaves() {
        std::error_code ec;
        if (!dir.empty()) fs::remove_all(dir, ec);
    }
};
TempSaves g_tempSaves;

// "auto" (o padrao): pt-BR se um pacote de mods\ tiver as falas em pt-BR, senao en-US, senao desligadas (com aviso).
std::string resolveAutoSubtitles() {
    const auto mods = bs::native::localization::ModPackages::discover(exeDir() / L"mods", "T13001N V1.000");
    for (const char* lang : {"pt-BR", "en-US"})
        if (const auto f = mods.find("legendas/falas.{idioma}.toml", lang); f && f->language == lang) {
            std::fprintf(stderr, "[legenda] idioma automatico: %s (pacote %s)\n", lang, f->package->id.c_str());
            return lang;
        }
    std::fprintf(stderr, "[legenda] idioma automatico: nenhum pacote de legendas em mods\\; legendas desativadas\n");
    return {};
}

// Idioma das legendas: --legendas > legendas.idioma do instalacao.toml > "auto" (padrao). "" = desligadas.
std::string subtitlesLanguage(const Args& a) {
    std::string out;
    if (a.subtitlesSet) {
        if (bs::native::install::parse_subtitles(a.subtitles, &out)) return out == "auto" ? resolveAutoSubtitles() : out;
        std::fprintf(stderr, "[legenda] --legendas \"%s\" invalido; usando o automatico\n", a.subtitles.c_str());
        return resolveAutoSubtitles();
    }
    std::string warn;
    const auto  cfg = bs::native::install::read_config(exeDir(), &warn);
    if (!warn.empty()) std::fprintf(stderr, "[legenda] %s\n", warn.c_str());
    return cfg.subtitles == "auto" ? resolveAutoSubtitles() : cfg.subtitles;
}

fs::path savesRoot(const Args& a) {
    // Precedencia: --saves-pasta > --saves-portatil > instalacao.toml (modo portatil gravado pelo
    // BlueStinger-Recompiler; vale ao abrir o executavel direto) > no --headless, cartao temporario >
    // Documentos. O portatil fica dentro da propria instalacao, entao vale tambem no --headless.
    std::string                     warn;
    const bs::native::install::InstallConfig cfg = bs::native::install::read_config(exeDir(), &warn);
    if (!warn.empty()) std::fprintf(stderr, "[saves] %s\n", warn.c_str());
    if (!a.savesDir.empty() || a.savesPortable || cfg.saves == bs::native::install::SavesLocation::Portable) {
        if (a.savesDir.empty() && !a.savesPortable) std::fprintf(stderr, "[saves] modo portatil (instalacao.toml)\n");
        return bs::native::install::saves_root(a.savesDir, a.savesPortable, exeDir(), cfg, {}, {});
    }
    if (a.headless && a.savesImport.empty() && a.savesExport.empty()) {
        if (g_tempSaves.dir.empty())
            g_tempSaves.dir = fs::temp_directory_path() / (L"bs-saves-headless-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ec;
        fs::remove_all(g_tempSaves.dir, ec);
        return g_tempSaves.dir;
    }
    PWSTR docs = nullptr;
    fs::path root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &docs)) && docs)
        root = fs::path(docs) / L"DC-Recomp" / L"Blue Stinger" / L"saves";
    CoTaskMemFree(docs);
    if (root.empty()) {
        root = userDir() / L"saves";
        std::fprintf(stderr, "[saves] pasta Documentos indisponivel: usando %s\n", utf8(root).c_str());
    }
    return root;
}

bs::GameOptions gameOptions(const Args& a) {
    bs::GameOptions opt;
    opt.data              = a.gameData;
    opt.modAssets         = exeDir() / L"mods" / L"assets";  // mods de arquivos (opcional)
    opt.flash             = a.flash;
    opt.files             = a.files;
    opt.time              = a.time;
    opt.controls          = a.controls;
    opt.saves             = a.saves;
    if (a.saves == bs::SavesProvider::Native) opt.savesRoot = savesRoot(a);
    opt.video             = a.video;
    opt.render            = a.render == 1 ? bs::RenderProvider::Native : bs::RenderProvider::Software;
    opt.audioEffects      = a.audioEffects;
    opt.audioVoices       = a.audioVoices;
    opt.audioMusic        = a.audioMusic;
    opt.dubbing           = a.dubbing;
    opt.modAudio          = exeDir() / L"mods" / L"audio";  // dublagens e audio de mods (opcional)
    opt.modVideo          = exeDir() / L"mods" / L"video";  // filmes e legendas de mods (opcional)
    opt.subtitles         = subtitlesLanguage(a);
    opt.registerFunctions = [] { bsg::register_functions(); };
    if (a.cameraMode >= 0) opt.camera.mode = st::CameraMode(a.cameraMode);
    opt.realtime          = a.headless == 0;
    opt.vblankLimit       = a.headless;
    return opt;
}

const char* kUsage =
    "uso: bluestinger [--data <pasta>] [--flash <arquivo>] [--map <arquivo>] [--scale N] [--capture <dir>]\n"
    "     bluestinger [--data <pasta>] --headless <vblanks> [--dump <arquivo.bmp>] [--dump-every N --capture <dir>]\n"
    "     bluestinger [--data <pasta>] --verify-data\n"
    "     bluestinger --version\n"
    "     --arquivos nativo|legado: provedor dos arquivos do jogo (padrao: nativo)\n"
    "     --tempo nativo|legado|experimental: provedor de tempo e eventos (padrao: nativo;\n"
    "       experimental = N2b-2, tempo logico sem ciclos estimados, em validacao)\n"
    "     --controles nativo|legado: provedor dos controles (padrao: nativo = o SDK de controles alimentado\n"
    "       direto, sem controle no Maple; legado = pelo Maple emulado, fallback temporario)\n"
    "     --saves nativo|legado: saves (padrao: nativo = arquivos em Documentos\\DC-Recomp\\Blue Stinger\\saves;\n"
    "       legado = sem VMU, fallback temporario; --saves-portatil = saves\\ ao lado do executavel;\n"
    "       --saves-pasta <dir> = outra pasta)\n"
    "     --saves-importar <arquivo.vms> | --saves-exportar <arquivo.vms> [--saves-nome BLUESTIN.00n]:\n"
    "       copia um save de VMU (VMS) para a pasta de saves, ou de la para fora, e sai\n"
    "     --video nativo|legado: filmes (padrao: nativo = MP4 do Builder em data\\video, mods em mods\\video;\n"
    "       legado = Sofdec original, fallback temporario)\n"
    "     --render nativo|software: renderizador (padrao na janela: nativo = Vulkan, com recuo para o software sem\n"
    "       Vulkan ou se o dispositivo falhar; padrao no --headless: software = rasterizador de referencia, e --render\n"
    "       nativo o troca pelo Vulkan nas baterias; BS_RENDER muda o padrao;\n"
    "       o graficos.toml do menu vale sobre o padrao da janela)\n"
    "     --apresentacao vulkan|sdl: janela (padrao: vulkan = apresentacao direta pelo dispositivo do renderizador, a\n"
    "       resolucao interna sem passar pela CPU; sdl = SDL_Renderer, recuo; BS_APRESENTACAO=sdl muda o padrao)\n"
    "     --resolucao-interna <altura>: resolucao interna do Vulkan so nesta execucao (480 = original; 720, 1080)\n"
    "     --legendas <idioma>|auto|nenhuma: legendas de mods nos filmes e nas falas (padrao: auto = pt-BR se o pacote\n"
    "       estiver instalado, senao en-US; ou legendas.idioma do instalacao.toml; en-US recua para en)\n"
    "     --audio nativo|legado: os tres provedores de audio de uma vez (padrao: nativo; legado = SDK de som e driver\n"
    "       Manatee originais na AICA emulada, fallback temporario); as opcoes abaixo valem sobre ela\n"
    "     --audio-efeitos nativo|legado: efeitos sonoros (padrao: nativo = BS_SND.MLT do disco pelo mixer nativo)\n"
    "     --audio-falas nativo|legado: falas (padrao: nativo = WAV do disco e dublagens de mods)\n"
    "     --audio-musica nativo|legado: musica (padrao: nativo = sequenciador e sintetizador Manatee em C++ com os\n"
    "       M*.MPB/MSB e o MANATEE.DRV do disco, e o DSP da AICA)\n"
    "     --camera original|livre: camera livre no analogico direito so nesta execucao (padrao: [camera] do\n"
    "       controles.toml, ou original; original = a camera do jogo, sem nada instalado)\n"
    "     --map <arquivo>: teclado no formato antigo ACAO = TECLA (substitui o [teclado] de controles.toml)\n"
    "Os dados do jogo ficam em data\\ ao lado do executavel (preparados pelo Blue Stinger Builder).\n"
    "Controles: controles.toml na pasta do usuario (ao lado do bluestinger.log), criado na primeira execucao.\n";

const char* kNoCdi =
    "O jogo nao le mais o CDI: os dados ficam na pasta data\\ ao lado do executavel, preparada\n"
    "pelo Blue Stinger Builder a partir do seu CDI. Rode o executavel sem argumentos.";

int mainHeadless(Args& a) {
    std::string err;
    if (!validateData(a.gameData, a.files, err)) {
        std::fprintf(stderr, "erro: %s\n", err.c_str());
        return 1;
    }
    // desenvolvimento: gameplay assistido (app/assist.h); a entrada vem so dos comandos
    std::unique_ptr<bs::Assist>      assist;
    std::unique_ptr<ni::InputSystem> input;
    std::string                      warning;  // sem janela: nao ha mapa de usuario a avisar
    if (const wchar_t* dir = _wgetenv(L"BS_ASSISTIDO"); dir && *dir) {
        if (std::getenv("BS_INPUT_SCRIPT")) throw std::runtime_error("BS_ASSISTIDO e BS_INPUT_SCRIPT sao exclusivos");
        std::shared_ptr<bs::LiveScriptInput> live = bs::make_live_script_input();
        assist = std::make_unique<bs::Assist>(fs::path(dir), live.get());
        input  = setupInput(a, false, warning, live);
    }
    else input = setupInput(a, false, warning);
    bs::GameOptions opt = gameOptions(a);
    opt.input           = input.get();
    if (assist) opt.vblankHook = [&assist](const bs::GameOptions::VblankView& v) { return assist->onVblank(v); };
    bs::GameHost host;
    if (a.dumpEvery && !a.capture.empty()) opt.captureEvery = a.dumpEvery, opt.capture = headlessCapture(a);
    host.start(std::move(opt));
    if (a.internalHeight) host.setRenderScale(float(a.internalHeight) / 480.f);
    const int rc = runHeadless(host, a);
    host.requestStop();
    host.join();
    ib::install(nullptr);  // a thread do jogo ja parou: a fonte pode sair antes do InputSystem
    return rc;
}

// --saves-importar / --saves-exportar: um arquivo VMS (o de uma VMU, p.ex. extraido do Flycast) entra
// na pasta de saves nativa como o slot --saves-nome, ou sai dela. A importacao so aceita VMS integro
// (cabecalho e CRC) e usa a mesma gravacao atomica do jogo; nao sobrescreve save de versao futura.
int mainSavesTransfer(const Args& a) {
    namespace ns          = bs::native::saves;
    const fs::path   root = savesRoot(a);
    ns::FileStoreOptions so;
    so.log = [](const std::string& m) { std::fprintf(stderr, "%s\n", m.c_str()); };
    auto                store = ns::make_file_store(root, so);
    const bool          imp   = !a.savesImport.empty();
    if (!imp) {  // o destino nao pode ser um arquivo do proprio slot (trocaria o envelope por um VMS puro)
        std::error_code ec;
        const fs::path  out = fs::weakly_canonical(a.savesExport, ec);
        for (const wchar_t* ext : {L".bsv", L".bsv.bak", L".bsv.tmp"}) {
            const fs::path own = fs::weakly_canonical(root / L"card0" / (fs::path(a.savesName).wstring() + ext), ec);
            if (fs::equivalent(out, own, ec) || out == own) {
                std::fprintf(stderr, "erro: o destino da exportacao e o proprio arquivo do slot %s\n", a.savesName.c_str());
                return 1;
            }
        }
    }
    const ns::SaveError e     = imp ? ns::import_vms(*store, 0, a.savesName, a.savesImport)
                                    : ns::export_vms(*store, 0, a.savesName, a.savesExport);
    if (e != ns::SaveError::None) {
        std::fprintf(stderr, "erro: %s %s: %s\n", imp ? "importar" : "exportar", a.savesName.c_str(), ns::save_error_name(e));
        return 1;
    }
    std::printf("%s %s %s %s (pasta %s)\n", imp ? "importado" : "exportado", utf8(imp ? a.savesImport : a.savesExport).c_str(),
                imp ? "->" : "<-", a.savesName.c_str(), utf8(root).c_str());
    return 0;
}

int mainVerifyData(const Args& a) {
    std::string err;
    if (!validateData(a.gameData, a.files, err)) {
        std::fprintf(stderr, "erro: %s\n", err.c_str());
        return 1;
    }
    const std::string bad = bs::disc::check_runtime_files(a.gameData);
    if (!bad.empty()) {
        std::fprintf(stderr, "erro: %s\n", bad.c_str());
        return 1;
    }
    std::printf("dados do jogo conferidos: %s\n", utf8(a.gameData).c_str());
    return 0;
}

int mainWindow(Args& a) {
    if (const wchar_t* dir = _wgetenv(L"BS_ASSISTIDO"); dir && *dir) {
        std::fprintf(stderr, "erro: BS_ASSISTIDO exige --headless\n");
        return 2;
    }
    // dados validados antes de abrir a janela: sem eles, so a mensagem
    std::string err;
    if (!validateData(a.gameData, a.files, err)) {
        std::fprintf(stderr, "erro: %s\n", err.c_str());
        nativeBox("Os dados do jogo nao puderam ser carregados.\n\n" + err, MB_ICONERROR);
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        const std::string msg = std::string("nao foi possivel iniciar o video: ") + SDL_GetError();
        std::fprintf(stderr, "erro: %s\n", msg.c_str());
        nativeBox(msg, MB_ICONERROR);
        return 1;
    }
    const std::string title = std::string(kTitle) + " " + BS_VERSION;
    // N7c: apresentacao direta pelo Vulkan (o mesmo dispositivo do renderizador: a resolucao interna nao passa pela
    // CPU) quando ha Vulkan; senao, ou com --apresentacao sdl / BS_APRESENTACAO=sdl, o SDL_Renderer
    bool wantVk = a.present != 0;
    if (const char* e = std::getenv("BS_APRESENTACAO"); a.present < 0 && e && std::strcmp(e, "sdl") == 0) wantVk = false;
    if (wantVk && SDL_Vulkan_LoadLibrary(nullptr)) {
        Uint32             n   = 0;
        const char* const* ext = SDL_Vulkan_GetInstanceExtensions(&n);
        if (ext && n) bs::native::render::vulkan_shared_configure(std::vector<std::string>(ext, ext + n));
        else wantVk = false;
    } else {
        wantVk = false;
    }
    SDL_Window* window = SDL_CreateWindow(title.c_str(), 640 * a.scale, 480 * a.scale,
                                          SDL_WINDOW_RESIZABLE | (wantVk ? SDL_WINDOW_VULKAN : 0));
    PresenterRef  pres;
    SDL_Renderer* renderer = nullptr;
    if (window && wantVk) {
        std::string perr;
        pres.p = bs::make_vulkan_presenter(window, &perr);
        if (!pres.p) std::fprintf(stderr, "[apresentacao] Vulkan indisponivel (%s): SDL\n", perr.c_str());
    }
    if (window && !pres.p) {
        renderer = SDL_CreateRenderer(window, nullptr);
        pres.p   = bs::make_sdl_presenter(window, renderer);
    }
    auto cleanup = [&] {
        pres.p.reset();  // a superficie Vulkan antes da janela
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    };
    if (!pres.p) {
        errorBox(window, std::string("nao foi possivel criar a janela: ") + SDL_GetError());
        cleanup();
        return 1;
    }
    pres.p->set_vsync(true);  // a janela apresenta na frequencia do monitor (o menu aplica o V-Sync escolhido)

    int rc = 0;
    // modo espectador: o roteiro (setupInput o escolhe antes do teclado) e a sala pelo registro de arquivos
    g_spectator.on = std::getenv("BS_INPUT_SCRIPT") != nullptr;
    if (g_spectator.on && !bs::g_file_log)
        bs::file_log_init(utf8(fs::temp_directory_path() / L"bs-espectador-arquivos.tsv"));
    std::string                      warning;
    std::unique_ptr<ni::InputSystem> input = setupInput(a, true, warning);
    // controles pelo SDL3; sem o subsistema de controles, o XInput de reserva. No modo espectador o jogo so ve o roteiro
    // (fonte de teste), mas o controle ainda chega a camera livre (N7c: InputSystem::peek)
    if (input->map().options.backend == "sdl" && !SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        if (g_spectator.on)  // o XInput de reserva so e lido na amostra do jogo, que o roteiro substitui
            std::fprintf(stderr, "[controles] SDL3 Gamepad indisponivel (%s): no modo espectador, sem controle para a camera\n",
                         SDL_GetError());
        else {
            std::fprintf(stderr, "[controles] SDL3 Gamepad indisponivel (%s): usando XInput\n", SDL_GetError());
            input->enable_xinput();
        }
    }
    if (!warning.empty()) {
        std::fprintf(stderr, "aviso: %s\n", warning.c_str());
        nativeBox(warning, MB_ICONWARNING);
    }
    if (const wchar_t* rec = _wgetenv(L"BS_GRAVAR"); rec && *rec) {
        const char* why = g_spectator.on ? "BS_GRAVAR e BS_INPUT_SCRIPT sao exclusivos"
                          : a.time == bs::TimeProviderKind::Legacy ? "BS_GRAVAR exige --tempo nativo ou experimental (roteiro por passo)"
                                                                   : nullptr;
        if (why) {
            std::fprintf(stderr, "erro: %s\n", why);
            cleanup();
            return 2;
        }
        g_recorder      = std::make_unique<Recorder>();
        g_recorder->dir = fs::path(rec).parent_path();
        g_recorder->f   = _wfopen(rec, L"w");
        if (!g_recorder->f) {
            std::fprintf(stderr, "erro: BS_GRAVAR: nao abre %s\n", utf8(rec).c_str());
            cleanup();
            return 2;
        }
        const char*    base  = std::getenv("BS_GRAVAR_BASE");
        const char*    until = std::getenv("BS_GRAVAR_ATE");
        const uint64_t ate   = base && *base && until ? std::strtoull(until, nullptr, 10) : 0;
        std::fprintf(g_recorder->f, "# gravacao de sessao (BS_GRAVAR, %s %s)%s\nindice passo\n", kTitle, BS_VERSION,
                     ate ? (" continuada de um roteiro ate o passo " + std::to_string(ate)).c_str() : "");
        std::fflush(g_recorder->f);
        try {
            FILE* f         = g_recorder->f;
            g_recorder->rec = std::make_unique<ib::SessionRecorder>(
                [f](const std::string& line) { std::fprintf(f, "%s\n", line.c_str()), std::fflush(f); },
                base && *base ? readFile(base) : std::string(), ate);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "erro: BS_GRAVAR_BASE: %s\n", ex.what());
            std::fclose(g_recorder->f);
            g_recorder.reset();
            cleanup();
            return 2;
        }
        input->set_filter([](const ni::SampleContext&, const ni::ControllerState& s) { return g_recorder->rec->filter(s); });
        std::printf("[gravacao] gravando em %s\n", utf8(rec).c_str());
    }
    // N7c: preferencias do menu interno (pasta do usuario). Precedencia: linha de comando > estes arquivos >
    // instalacao.toml > padroes (software; legendas en-US ligadas; volumes 100).
    const fs::path  prefsDir = userDir();
    bs::Preferences prefs, persisted;
    bool            renderOverride = false, subtitleOverride = false, cameraOverride = false;
    {
        std::string w1, w2, w3, w4;
        bool        legFile = false;
        prefs.graphics  = st::read_graphics(prefsDir, &w1);
        prefs.audio     = st::read_audio(prefsDir, &w2);
        prefs.subtitles = st::read_subtitles(prefsDir, &w3, &legFile);
        prefs.camera    = st::read_camera(prefsDir, &w4);
        for (const std::string& w : {w1, w2, w3, w4})
            if (!w.empty()) std::fprintf(stderr, "[menu] %s\n", w.c_str());
        persisted        = prefs;
        renderOverride   = a.renderExplicit;
        cameraOverride   = a.cameraMode >= 0;
        if (cameraOverride) prefs.camera.mode = st::CameraMode(a.cameraMode);
        subtitleOverride = a.subtitlesSet || !legFile;  // sem legendas.toml, o idioma vem do instalacao.toml
        if (a.renderExplicit) prefs.graphics.vulkan = a.render == 1;
        else a.render = prefs.graphics.vulkan ? 1 : 0;
        if (!a.subtitlesSet && legFile) {
            a.subtitles    = prefs.subtitles.enabled ? prefs.subtitles.language : "nenhuma";
            a.subtitlesSet = true;
        } else {
            const std::string lang    = subtitlesLanguage(a);
            prefs.subtitles.enabled   = !lang.empty();
            if (!lang.empty()) prefs.subtitles.language = lang;
        }
    }
    st::Capabilities caps;  // o que o renderizador ja implementa (o resto: "em breve" no menu)
    uint32_t         gpuMaxHeight = 480;
    {
        std::string vkErr;
        const auto  dev = bs::native::render::vulkan_shared(&vkErr);  // o dispositivo do processo (o mesmo da janela)
        caps.vulkan     = dev != nullptr;
        if (dev) gpuMaxHeight = dev->stats().max_internal_height;
        if (!caps.vulkan) std::fprintf(stderr, "[menu] Vulkan indisponivel (%s): so o renderizador em software\n", vkErr.c_str());
    }
    caps.max_height = 480, caps.features.window_modes = true, caps.features.vsync = true;
    // N7c: resolucao interna com o Vulkan e a apresentacao direta (pela do SDL a imagem maior nao chega a janela): ate 4K
    // (2160), no maximo o que a GPU comporta (o lado das imagens e a memoria local; VulkanStats::max_internal_height)
    if (caps.vulkan && std::strcmp(pres.p->name(), "Vulkan") == 0) {
        caps.features.scaling = true, caps.max_height = (std::min)(2160u, gpuMaxHeight);
        caps.features.color = caps.features.shaders = true;  // N7c (secao 21): pos-processamento na apresentacao direta
        caps.features.tonemap = caps.features.sharpen = caps.features.bloom = caps.features.crt = true;
    }
    // shaders de pos-processamento de mods (mods\shaders\<nome>): validados aqui; os invalidos ficam com o motivo
    auto postShaders = bs::native::render::discover_post_shaders(exeDir() / L"mods" / L"shaders");
    for (const auto& p : postShaders)
        std::fprintf(stderr, "[pos] shader de mod %s: %s\n", p.name.c_str(), p.error.empty() ? "valido" : ("INVALIDO: " + p.error).c_str());
    if (caps.vulkan) std::fprintf(stderr, "[menu] resolucao interna: ate %u (GPU: ate %u)\n", caps.max_height, gpuMaxHeight);
    prefs.graphics = st::clamp_to(prefs.graphics, caps);
    if (!prefs.graphics.vulkan) a.render = 0;
    std::vector<std::string> languages;  // os idiomas dos pacotes de legendas encontrados
    const auto modPackages = bs::native::localization::ModPackages::discover(exeDir() / L"mods", "T13001N V1.000");
    for (const auto& pr : modPackages.problems())
        std::fprintf(stderr, "[menu] pacote de mod recusado (%s): %s\n", utf8(pr.dir.filename()).c_str(), pr.message.c_str());
    for (const auto& pkg : modPackages.packages())
        for (const auto& l : pkg.languages)
            if (std::find(languages.begin(), languages.end(), l) == languages.end()) languages.push_back(l);
    if (std::find(languages.begin(), languages.end(), prefs.subtitles.language) == languages.end())
        languages.insert(languages.begin(), prefs.subtitles.language);
    {
        std::string l;
        for (const auto& x : languages) l += (l.empty() ? "" : ", ") + x;
        std::fprintf(stderr, "[menu] idiomas das legendas: %s (atual: %s)\n", l.c_str(), prefs.subtitles.language.c_str());
    }
    uint64_t endVblank = 0;
    {
        bs::GameHost host;
        bs::GameOptions opt = gameOptions(a);
        opt.input           = input.get();
        opt.camera          = prefs.camera;  // N7c: [camera] do controles.toml (--camera troca o modo so nesta execucao)
        if (g_spectator.on || g_recorder)
            opt.vblankHook = [](const bs::GameOptions::VblankView& v) {
                g_spectator.step = v.step;
                uint32_t on      = 0;  // PDS_PERIPHERAL da porta A, campo `on` (+8)
                std::memcpy(&on, v.ram + ((bs::transitional::pad::kTable + 8) & 0xFFFFFF), 4);
                g_spectator.buttons = on;
                return true;
            };
        host.start(std::move(opt));
        InGameMenu menu(host, *input, window, pres, prefsDir, prefs, persisted, renderOverride, subtitleOverride, cameraOverride, caps,
                        languages);
        if (g_spectator.on) menu.disable();
        menu.set_shaders(std::move(postShaders));
        menu.apply(prefs, true);
        if (a.internalHeight) host.setRenderScale(float(a.internalHeight) / 480.f);  // so nesta execucao
        rc = runWindow(host, *input, a, window, pres, renderer, menu);
        // a thread do jogo le a fonte de entrada: para e espera ANTES de destrui-la, em todos
        // os caminhos (revisao Codex 2026-10-14)
        host.requestStop();
        host.join();
        endVblank = host.vblanks();
    }
    if (g_recorder) {  // o ponto final (passo e o VBlank em que o jogo parou): a reproducao para no mesmo VBlank
        g_recorder->rec->comment("fim passo " + std::to_string(g_spectator.step.load()) + " vblank " + std::to_string(endVblank));
        std::fclose(g_recorder->f);
        g_recorder.reset();
    }
    ib::install(nullptr);
    cleanup();
    return rc;
}

} // namespace

int main(int, char**) {
    setupLog(userDir());
    setupCrashLog();
    Args a;
    if (!parseArgs(a)) {
        std::fprintf(stderr, "%s", kUsage);
        nativeBox(kUsage, MB_ICONINFORMATION);
        return 2;
    }
    if (a.version) {
        std::printf("Blue Stinger (recompilado) %s, compilado em %s\n", BS_VERSION, BS_BUILD_DATE);
        return 0;
    }
    if (!a.positional.empty()) {  // antes era a imagem CDI: explica em vez de ignorar
        std::fprintf(stderr, "erro: argumento %s nao e usado.\n%s\n", utf8(a.positional).c_str(), kNoCdi);
        if (!a.headless && !a.verifyData) nativeBox(kNoCdi, MB_ICONINFORMATION);
        return 2;
    }
    if (a.gameData.empty()) a.gameData = exeDir() / L"data";
    std::fprintf(stderr, "%s %s (%s); dados: %s\n", kTitle, BS_VERSION, BS_BUILD_DATE, utf8(a.gameData).c_str());
    try {
        if (a.verifyData) return mainVerifyData(a);
        if (!a.savesImport.empty() && !a.savesExport.empty()) {
            std::fprintf(stderr, "erro: --saves-importar e --saves-exportar sao exclusivos\n");
            return 2;
        }
        if (!a.savesImport.empty() || !a.savesExport.empty()) return mainSavesTransfer(a);
        return a.headless ? mainHeadless(a) : mainWindow(a);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        if (!a.headless) nativeBox(e.what(), MB_ICONERROR);
        return 1;
    }
}
