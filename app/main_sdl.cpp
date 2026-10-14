// bluestinger: executavel nativo. Janela, apresentacao, dialogo de arquivo e eventos pelo
// SDL3 (so neste arquivo); o jogo roda no GameHost (outra thread), a partir da imagem de
// disco do usuario. Nenhum dado do jogo vem embutido: o executavel le o CDI ao iniciar.
//
//   bluestinger [imagem.cdi] [--flash <arquivo>] [--map <arquivo>] [--scale N] [--capture <dir>]
//   bluestinger <imagem.cdi> --headless <vblanks> [--dump <arquivo.bmp>]   (sem janela)
//   bluestinger --version
// Sem imagem na linha de comando, a janela pede o arquivo (dialogo do Windows) e lembra o
// ultimo escolhido em bluestinger.ini. Caminhos em UTF-16/UTF-8 (nomes com acentos ou
// outros alfabetos funcionam). Subsistema grafico: sem console, o log vai para
// bluestinger.log ao lado do executavel (ou em %LOCALAPPDATA%\BlueStingerRecomp).
#include "boot/native_boot.h"
#include "bs_version.h"
#include "disc/disc_image.h"
#include "funcs.h"
#include "game_host.h"
#include "input/input.h"

#include <SDL3/SDL.h>

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

namespace fs = std::filesystem;

namespace {

const char* kTitle = "Blue Stinger (recompilado)";

struct Args {
    fs::path disc, flash, map, dump, capture;
    uint64_t headless = 0;
    int      scale    = 2;
    bool     version  = false;
};

std::string utf8(const fs::path& p) { return bs::disc::pathText(p); }
fs::path    fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

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
        else if (s == L"--headless" && (v = next())) a.headless = std::wcstoull(v->c_str(), nullptr, 10);
        else if (s == L"--scale" && (v = next())) a.scale = _wtoi(v->c_str());
        else if (s == L"--version") a.version = true;
        else if (!s.empty() && s[0] != L'-' && a.disc.empty()) a.disc = s;
        else return false;
    }
    if (a.disc.empty())
        if (const wchar_t* e = _wgetenv(L"BS_DISC_IMAGE")) a.disc = e;
    return a.scale >= 1 && a.scale <= 8 && (a.headless == 0 || !a.disc.empty());
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

// Pasta de dados do usuario: ao lado do executavel se der para gravar la; senao
// %LOCALAPPDATA%\BlueStingerRecomp.
fs::path dataDir() {
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

fs::path readLastDisc(const fs::path& dir) {
    std::ifstream f(dir / L"bluestinger.ini", std::ios::binary);
    std::string   line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("cdi=", 0) == 0) return fromUtf8(line.substr(4));
    }
    return {};
}

void saveLastDisc(const fs::path& dir, const fs::path& disc) {
    std::ofstream f(dir / L"bluestinger.ini", std::ios::binary | std::ios::trunc);
    f << "# Blue Stinger (recompilado): ultima imagem de disco usada\ncdi=" << utf8(disc) << "\n";
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

// Dialogo de abertura do SDL (assincrono): espera a resposta processando eventos.
// O callback pode rodar em outra thread e depois de um pedido de fechar: o estado e
// compartilhado (a ultima referencia o libera) e protegido por mutex (revisao Codex).
struct DiscChoice {
    fs::path    path;   // vazio = cancelado ou erro
    std::string error;  // falha do proprio dialogo (SDL distingue de cancelar)
};

DiscChoice chooseDisc(SDL_Window* window, const fs::path& start) {
    struct State {
        std::mutex m;
        bool       done = false;
        DiscChoice result;
    };
    auto st = std::make_shared<State>();
    static const SDL_DialogFileFilter filters[] = {{"Imagem de disco CDI", "cdi"}, {"Todos os arquivos", "*"}};
    const std::string                 startDir   = start.empty() ? std::string() : utf8(start.parent_path());
    SDL_ShowOpenFileDialog(
        [](void* ud, const char* const* list, int) {
            std::unique_ptr<std::shared_ptr<State>> ref(static_cast<std::shared_ptr<State>*>(ud));
            State&                                  s = **ref;
            std::lock_guard                         lock(s.m);
            if (!list) s.result.error = std::string("dialogo de arquivo falhou: ") + SDL_GetError();
            else if (list[0]) s.result.path = fromUtf8(list[0]);
            s.done = true;
        },
        new std::shared_ptr<State>(st), window, filters, 2, startDir.empty() ? nullptr : startDir.c_str(), false);
    for (;;) {
        {
            std::lock_guard lock(st->m);
            if (st->done) return st->result;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT) return {};  // o callback ainda pode vir: o estado continua vivo
        SDL_Delay(10);
    }
}

// Imagem valida = executavel de boot com o hash da versao recompilada (NTSC-U V1.000).
bool validateDisc(const fs::path& disc, std::string& err) {
    try {
        bs::boot_read_disc(disc);
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

int runHeadless(bs::GameHost& host, const Args& a) {
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

int runWindow(bs::GameHost& host, bs::KeyEventInput* keys, const Args& a, SDL_Window* window,
              SDL_Renderer* renderer) {
    SDL_Texture*  texture = nullptr;
    uint32_t      texW = 0, texH = 0;
    bs::GameFrame frame;
    uint64_t      seq  = 0;
    bool          quit = false, fullscreen = false;
    uint64_t      lastTitle = SDL_GetTicks();
    int           captures  = 0;
    int           gfxFailures = 0;
    std::string   gfxError;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
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
                    std::printf("captura %s: %s\n", utf8(path).c_str(), writeBmp(path, frame) ? "ok" : "FALHOU");
                    break;
                }
                if (e.key.key == SDLK_F11 && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen);
                    break;
                }
                const std::string n = keyName(e.key.key);
                if (!n.empty()) keys->key(n, e.type == SDL_EVENT_KEY_DOWN);
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_LOST: keys->releaseAll(); break;
            default: break;
            }
        }
        // falhas de apresentacao viram erro (janela preta com codigo 0 pareceria sucesso):
        // criar a textura falha de vez; as demais so se persistirem (~2 s seguidos)
        bool ok = true;
        if (host.latestFrame(frame, seq) && frame.width && frame.height) {
            if (!texture || texW != frame.width || texH != frame.height) {
                if (texture) SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                            int(frame.width), int(frame.height));
                texW = frame.width, texH = frame.height;
                if (!texture) {
                    gfxError = std::string("nao foi possivel criar a textura do quadro: ") + SDL_GetError();
                    break;
                }
            }
            ok &= SDL_UpdateTexture(texture, nullptr, frame.argb.data(), int(frame.width * 4));
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        ok &= SDL_RenderClear(renderer);
        if (texture) ok &= SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        ok &= SDL_RenderPresent(renderer);
        gfxFailures = ok ? 0 : gfxFailures + 1;
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
            SDL_SetWindowTitle(window, t);
        }
    }
    host.requestStop();
    host.join();
    printStats(host);
    const std::string err = host.error();
    if (!err.empty()) errorBox(window, "O jogo parou com erro:\n" + err);
    else if (!gfxError.empty()) errorBox(window, "Falha de video:\n" + gfxError);
    if (texture) SDL_DestroyTexture(texture);
    return err.empty() && gfxError.empty() ? 0 : 1;
}

// Entrada: teclado pela janela (mapa opcional) + XInput; roteiro so para testes.
bs::KeyEventInput* setupInput(const Args& a) {
    if (std::getenv("BS_INPUT_SCRIPT")) {
        bs::input_configure_from_env();
        return nullptr;
    }
    auto  kb   = bs::make_key_event_input(a.map.empty() ? "" : readFile(a.map));
    auto* keys = kb.get();
    std::vector<std::unique_ptr<bs::InputSource>> src;
    src.push_back(std::move(kb));
    src.push_back(bs::make_xinput_input());
    bs::input_set_source(bs::make_combined_input(std::move(src)));
    return keys;
}

bs::GameOptions gameOptions(const Args& a) {
    bs::GameOptions opt;
    opt.disc              = a.disc;
    opt.flash             = a.flash;
    opt.registerFunctions = [] { bsg::register_functions(); };
    opt.realtime          = a.headless == 0;
    opt.vblankLimit       = a.headless;
    return opt;
}

const char* kUsage =
    "uso: bluestinger [imagem.cdi] [--flash <arquivo>] [--map <arquivo>] [--scale N] [--capture <dir>]\n"
    "     bluestinger <imagem.cdi> --headless <vblanks> [--dump <arquivo.bmp>]\n"
    "     bluestinger --version\n";

int mainHeadless(Args& a) {
    std::string err;
    if (!validateDisc(a.disc, err)) {
        std::fprintf(stderr, "erro: imagem invalida (%s): %s\n", utf8(a.disc).c_str(), err.c_str());
        return 1;
    }
    setupInput(a);
    bs::GameHost host;
    host.start(gameOptions(a));
    const int rc = runHeadless(host, a);
    host.requestStop();
    host.join();
    bs::input_set_source(nullptr);
    return rc;
}

int mainWindow(Args& a, const fs::path& data) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        const std::string msg = std::string("nao foi possivel iniciar o video: ") + SDL_GetError();
        std::fprintf(stderr, "erro: %s\n", msg.c_str());
        nativeBox(msg, MB_ICONERROR);
        return 1;
    }
    const std::string title    = std::string(kTitle) + " " + BS_VERSION;
    SDL_Window*       window   = SDL_CreateWindow(title.c_str(), 640 * a.scale, 480 * a.scale, SDL_WINDOW_RESIZABLE);
    SDL_Renderer*     renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        errorBox(window, std::string("nao foi possivel criar a janela: ") + SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);  // a janela apresenta na frequencia do monitor
    // 4:3 por padrao: area logica de 640x480 com faixas pretas ao redimensionar
    SDL_SetRenderLogicalPresentation(renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    // imagem de disco: linha de comando ou dialogo; validada antes de iniciar o jogo
    const bool fromArgs = !a.disc.empty();
    int        rc       = 0;
    for (;;) {
        if (a.disc.empty()) {
            const DiscChoice c = chooseDisc(window, readLastDisc(data));
            if (!c.error.empty()) {  // falha nao e cancelamento: mensagem e codigo de erro
                errorBox(window, c.error);
                rc = 1;
                break;
            }
            a.disc = c.path;
            if (a.disc.empty()) break;  // cancelado
        }
        std::string err;
        if (validateDisc(a.disc, err)) break;
        errorBox(window, "Esta imagem nao pode ser usada:\n" + utf8(a.disc) + "\n\n" + err +
                             "\n\nE preciso a imagem CDI do Blue Stinger NTSC-U (T13001N V1.000).");
        if (fromArgs) {
            rc = 1;
            a.disc.clear();
            break;
        }
        a.disc.clear();  // escolher de novo
    }
    if (!a.disc.empty()) {
        saveLastDisc(data, a.disc);
        bs::KeyEventInput* keys = setupInput(a);
        bs::GameHost       host;
        host.start(gameOptions(a));
        if (keys) rc = runWindow(host, keys, a, window, renderer);
        else {
            errorBox(window, "BS_INPUT_SCRIPT exige --headless");
            rc = 2;
        }
        // a thread do jogo le a fonte de entrada: para e espera ANTES de destrui-la, em todos
        // os caminhos (revisao Codex 2026-10-14)
        host.requestStop();
        host.join();
        bs::input_set_source(nullptr);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}

} // namespace

int main(int, char**) {
    const fs::path data = dataDir();
    setupLog(data);
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
    std::fprintf(stderr, "%s %s (%s)\n", kTitle, BS_VERSION, BS_BUILD_DATE);
    try {
        return a.headless ? mainHeadless(a) : mainWindow(a, data);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        if (!a.headless) nativeBox(e.what(), MB_ICONERROR);
        return 1;
    }
}
