// Blue Stinger Builder (janela Win32): escolhe o CDI e a pasta de saida e roda o fluxo
// completo do builder_core numa thread, com progresso, log e cancelamento. No fim, abre a
// pasta ou inicia o jogo.
//   BlueStingerBuilder.exe [--disc <cdi>] [--out <pasta>] [--kit <pasta>] [--sdl3 <pasta>]
//                          [--auto]   (comeca sozinho e fecha no fim; codigo 0 = sucesso)
#include "builder_core.h"
#include "child_process.h"

#ifndef UNICODE
#define UNICODE  // IDC_ARROW, IDI_APPLICATION etc. em UTF-16
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bs::builder;

namespace {

enum : UINT { WM_BUILD_LOG = WM_APP + 1, WM_BUILD_PROGRESS, WM_BUILD_DONE };
enum : int {
    ID_DISC = 100, ID_DISC_BROWSE, ID_OUT, ID_OUT_BROWSE, ID_CLEAN, ID_BUILD, ID_CANCEL, ID_OPEN, ID_PLAY,
    ID_PROGRESS, ID_STATUS, ID_LOG, ID_EXPORT, ID_FROMPROJ
};

const wchar_t* const kTitle = L"Blue Stinger Builder";

struct Progress {
    Step         step;
    double       fraction;
    std::wstring text;
};

struct App {
    HWND               wnd = nullptr;
    HWND               disc, discBrowse, out, outBrowse, clean, exportProj, fromProj, build, cancel, open, play, progress, status, log;
    HFONT              font = nullptr;
    std::vector<HWND>  labels;
    fs::path           kit, sdl3;
    std::thread        worker;
    std::atomic<bool>  cancelFlag{false};
    bool               running = false, closeAfter = false, autoMode = false;
    int                exitCode = 0;
    Result             last;
} g;

fs::path exeDir() {
    std::wstring b(32768, L'\0');
    b.resize(GetModuleFileNameW(nullptr, b.data(), DWORD(b.size())));
    return fs::path(b).parent_path();
}

// preferencias: %LOCALAPPDATA%\BlueStingerRecomp\builder.ini (ultima imagem e saida, UTF-8)
fs::path iniPath() {
    PWSTR    p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) r = fs::path(p) / L"BlueStingerRecomp" / L"builder.ini";
    CoTaskMemFree(p);
    return r;
}

void loadPrefs(std::wstring& disc, std::wstring& out) {
    std::ifstream f(iniPath(), std::ios::binary);
    for (std::string l; std::getline(f, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.rfind("cdi=", 0) == 0) disc = widen(l.substr(4));
        else if (l.rfind("out=", 0) == 0) out = widen(l.substr(4));
    }
}

void savePrefs(const std::wstring& disc, const std::wstring& out) {
    const fs::path p = iniPath();
    if (p.empty()) return;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << "# Blue Stinger Builder\ncdi=" << narrow(disc) << "\nout=" << narrow(out) << "\n";
}

fs::path defaultOut() {
    PWSTR    p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p))) r = fs::path(p) / L"Blue Stinger Recompilado";
    CoTaskMemFree(p);
    return r;
}

std::wstring text(HWND h) {
    std::wstring s(size_t(GetWindowTextLengthW(h)), L'\0');
    GetWindowTextW(h, s.data(), int(s.size() + 1));
    return s;
}

void appendLog(const std::wstring& line) {
    // limita o controle (~1 MB): corta o comeco quando passa
    const int len = GetWindowTextLengthW(g.log);
    if (len > 1000000) {
        SendMessageW(g.log, EM_SETSEL, 0, len / 2);
        SendMessageW(g.log, EM_REPLACESEL, FALSE, LPARAM(L""));
    }
    const int end = GetWindowTextLengthW(g.log);
    SendMessageW(g.log, EM_SETSEL, end, end);
    SendMessageW(g.log, EM_REPLACESEL, FALSE, LPARAM((line + L"\r\n").c_str()));
}

// dialogo nativo de arquivo/pasta (Unicode)
std::wstring browse(bool folder, const std::wstring& current) {
    std::wstring       result;
    IFileOpenDialog*   dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    if (!folder) {
        const COMDLG_FILTERSPEC spec[] = {{L"Imagem de disco CDI (*.cdi)", L"*.cdi"}, {L"Todos os arquivos", L"*.*"}};
        dlg->SetFileTypes(2, spec);
        dlg->SetTitle(L"Imagem CDI do Blue Stinger (NTSC-U)");
    } else {
        dlg->SetTitle(L"Pasta onde o jogo compilado sera instalado");
    }
    if (!current.empty()) {
        std::error_code ec;
        fs::path        start = folder ? fs::path(current) : fs::path(current).parent_path();
        while (!start.empty() && !fs::is_directory(start, ec) && start != start.parent_path()) start = start.parent_path();
        IShellItem* item = nullptr;
        if (!start.empty() && SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            dlg->SetFolder(item);
            item->Release();
        }
    }
    if (SUCCEEDED(dlg->Show(g.wnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) result = p;
            CoTaskMemFree(p);
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

void setRunning(bool r) {
    g.running = r;
    for (HWND h : {g.disc, g.discBrowse, g.out, g.outBrowse, g.clean, g.exportProj, g.fromProj, g.build}) EnableWindow(h, !r);
    EnableWindow(g.cancel, r);
    const bool done = !r && g.last.ok;
    EnableWindow(g.open, done);
    EnableWindow(g.play, done);
}

void startBuild() {
    Request req;
    req.disc   = text(g.disc);
    req.outDir = text(g.out);
    req.kitDir = g.kit;
    req.sdl3Dir = g.sdl3;
    req.clean  = SendMessageW(g.clean, BM_GETCHECK, 0, 0) == BST_CHECKED;
    // projeto C++ editavel: sempre em <saida>\projeto (exportar, ou compilar a partir dele com os mods)
    const bool fromProj = SendMessageW(g.fromProj, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (SendMessageW(g.exportProj, BM_GETCHECK, 0, 0) == BST_CHECKED && !fromProj) req.exportProject = req.outDir / L"projeto";
    if (fromProj) req.fromProject = req.outDir / L"projeto";
    if ((req.disc.empty() && !fromProj) || req.outDir.empty()) {
        if (g.autoMode) {
            g.exitCode = 2;
            DestroyWindow(g.wnd);
            return;
        }
        MessageBoxW(g.wnd, L"Escolha a imagem CDI e a pasta de saída.", kTitle, MB_ICONWARNING);
        return;
    }
    savePrefs(req.disc.native(), req.outDir.native());
    SetWindowTextW(g.log, L"");
    SendMessageW(g.progress, PBM_SETPOS, 0, 0);
    g.cancelFlag = false;
    g.last       = {};
    setRunning(true);
    if (g.worker.joinable()) g.worker.join();
    g.worker = std::thread([req] {
        Callbacks cb;
        cb.cancel   = &g.cancelFlag;
        cb.log      = [](const std::string& l) { PostMessageW(g.wnd, WM_BUILD_LOG, 0, LPARAM(new std::wstring(widen(l)))); };
        cb.progress = [](Step s, double f, const std::string& m) {
            PostMessageW(g.wnd, WM_BUILD_PROGRESS, 0, LPARAM(new Progress{s, f, widen(m)}));
        };
        auto* r = new Result(build(req, cb));
        PostMessageW(g.wnd, WM_BUILD_DONE, 0, LPARAM(r));
    });
}

void onDone(Result* r) {
    std::unique_ptr<Result> res(r);
    g.last = *res;
    if (g.worker.joinable()) g.worker.join();
    setRunning(false);
    if (res->ok) {
        SendMessageW(g.progress, PBM_SETPOS, 1000, 0);
        SetWindowTextW(g.status, L"Pronto: BlueStinger.exe compilado e verificado.");
        g.exitCode = 0;
        if (!g.autoMode && !g.closeAfter)
            MessageBoxW(g.wnd,
                        (L"BlueStinger.exe foi compilado, verificado e instalado em:\n" + res->installDir.native() +
                         L"\n\nUse \"Jogar\" ou \"Abrir pasta\". O executável contém código derivado da sua cópia "
                         L"do jogo: use-o só localmente e não o publique.").c_str(),
                        kTitle, MB_ICONINFORMATION);
    } else {
        g.exitCode = res->cancelled ? 3 : 1;
        const std::wstring head = res->cancelled ? L"Cancelado." : L"Falhou em \"" + widen(step_name(res->failedStep)) + L"\".";
        SetWindowTextW(g.status, head.c_str());
        if (!g.autoMode && !g.closeAfter)
            MessageBoxW(g.wnd,
                        (head + L"\n\n" + widen(res->error) + L"\n\nNenhuma versão instalada anterior foi alterada." +
                         L"\nLog completo: " + res->logFile.native()).c_str(),
                        kTitle, res->cancelled ? MB_ICONINFORMATION : MB_ICONERROR);
    }
    if (g.autoMode || g.closeAfter) DestroyWindow(g.wnd);
}

HWND make(const wchar_t* cls, const wchar_t* txt, DWORD style, int id, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.wnd, HMENU(INT_PTR(id)),
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, WPARAM(g.font), TRUE);
    return h;
}

void layout() {
    RECT rc;
    GetClientRect(g.wnd, &rc);
    const UINT dpi = GetDpiForWindow(g.wnd);
    auto       s   = [dpi](int v) { return MulDiv(v, int(dpi), 96); };
    const int  W = rc.right, m = s(12), btn = s(96), h = s(24), gap = s(8);
    int        y = m;
    MoveWindow(g.labels[0], m, y, W - 2 * m, s(18), TRUE);
    y += s(20);
    MoveWindow(g.disc, m, y, W - 3 * m - btn + gap, h, TRUE);
    MoveWindow(g.discBrowse, W - m - btn, y, btn, h, TRUE);
    y += h + gap;
    MoveWindow(g.labels[1], m, y, W - 2 * m, s(18), TRUE);
    y += s(20);
    MoveWindow(g.out, m, y, W - 3 * m - btn + gap, h, TRUE);
    MoveWindow(g.outBrowse, W - m - btn, y, btn, h, TRUE);
    y += h + gap;
    MoveWindow(g.clean, m, y, W - 2 * m, h, TRUE);
    y += h;
    MoveWindow(g.exportProj, m, y, W - 2 * m, h, TRUE);
    y += h;
    MoveWindow(g.fromProj, m, y, W - 2 * m, h, TRUE);
    y += h + gap;
    const int bw = s(170);
    MoveWindow(g.build, m, y, bw, s(30), TRUE);
    MoveWindow(g.cancel, m + bw + gap, y, btn, s(30), TRUE);
    MoveWindow(g.play, W - m - btn, y, btn, s(30), TRUE);
    MoveWindow(g.open, W - m - 2 * btn - gap, y, btn, s(30), TRUE);
    y += s(30) + gap;
    MoveWindow(g.progress, m, y, W - 2 * m, s(18), TRUE);
    y += s(18) + s(4);
    MoveWindow(g.status, m, y, W - 2 * m, s(18), TRUE);
    y += s(18) + s(4);
    MoveWindow(g.log, m, y, W - 2 * m, std::max(int(rc.bottom) - y - m, s(40)), TRUE);
}

void createFont() {
    if (g.font) DeleteObject(g.font);
    NONCLIENTMETRICSW ncm{sizeof ncm};
    const UINT        dpi = GetDpiForWindow(g.wnd);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
}

LRESULT CALLBACK wndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.wnd = w;
        createFont();
        g.labels.push_back(make(L"STATIC", L"Imagem do jogo (CDI do Blue Stinger NTSC-U, sua cópia):", 0, -1));
        g.disc       = make(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, ID_DISC);
        g.discBrowse = make(L"BUTTON", L"Procurar...", WS_TABSTOP, ID_DISC_BROWSE);
        g.labels.push_back(make(L"STATIC", L"Pasta de saída (o jogo vai para a subpasta BlueStinger):", 0, -1));
        g.out       = make(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, ID_OUT);
        g.outBrowse = make(L"BUTTON", L"Procurar...", WS_TABSTOP, ID_OUT_BROWSE);
        g.clean     = make(L"BUTTON", L"Recompilar do zero (descarta a compilação incremental)", WS_TABSTOP | BS_AUTOCHECKBOX, ID_CLEAN);
        g.exportProj = make(L"BUTTON", L"Exportar também o projeto C++ editável (pasta projeto, para mods)", WS_TABSTOP | BS_AUTOCHECKBOX, ID_EXPORT);
        g.fromProj   = make(L"BUTTON", L"Compilar a partir do projeto editável (com os mods; CDI opcional)", WS_TABSTOP | BS_AUTOCHECKBOX, ID_FROMPROJ);
        g.build     = make(L"BUTTON", L"Gerar BlueStinger.exe", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_BUILD);
        g.cancel    = make(L"BUTTON", L"Cancelar", WS_TABSTOP, ID_CANCEL);
        g.open      = make(L"BUTTON", L"Abrir pasta", WS_TABSTOP, ID_OPEN);
        g.play      = make(L"BUTTON", L"Jogar", WS_TABSTOP, ID_PLAY);
        g.progress  = make(PROGRESS_CLASSW, L"", 0, ID_PROGRESS);
        SendMessageW(g.progress, PBM_SETRANGE32, 0, 1000);
        g.status = make(L"STATIC", L"Escolha a imagem e a pasta de saída.", SS_ENDELLIPSIS, ID_STATUS);
        g.log    = make(L"EDIT", L"", WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_BORDER, ID_LOG);
        SendMessageW(g.log, EM_SETLIMITTEXT, 4000000, 0);
        setRunning(false);
        layout();
        return 0;
    }
    case WM_SIZE: layout(); return 0;
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
        createFont();
        for (HWND c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SendMessageW(c, WM_SETFONT, WPARAM(g.font), TRUE);
        layout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        const UINT dpi = g.wnd ? GetDpiForWindow(g.wnd) : 96;
        mm->ptMinTrackSize = {MulDiv(560, int(dpi), 96), MulDiv(420, int(dpi), 96)};
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_DISC_BROWSE: {
            const std::wstring p = browse(false, text(g.disc));
            if (!p.empty()) SetWindowTextW(g.disc, p.c_str());
            break;
        }
        case ID_OUT_BROWSE: {
            const std::wstring p = browse(true, text(g.out));
            if (!p.empty()) SetWindowTextW(g.out, p.c_str());
            break;
        }
        case ID_BUILD: if (!g.running) startBuild(); break;
        case ID_CANCEL:
            g.cancelFlag = true;
            SetWindowTextW(g.status, L"Cancelando...");
            break;
        case ID_OPEN: ShellExecuteW(w, L"open", g.last.installDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case ID_PLAY:  // o jogo instalado usa os proprios dados (data\): sem o CDI
            ShellExecuteW(w, L"open", g.last.exe.c_str(), nullptr, g.last.installDir.c_str(), SW_SHOWNORMAL);
            break;
        }
        return 0;
    case WM_BUILD_LOG: {
        std::unique_ptr<std::wstring> l(reinterpret_cast<std::wstring*>(lp));
        appendLog(*l);
        return 0;
    }
    case WM_BUILD_PROGRESS: {
        std::unique_ptr<Progress> p(reinterpret_cast<Progress*>(lp));
        SendMessageW(g.progress, PBM_SETPOS, WPARAM(p->fraction * 1000), 0);
        if (p->step == Step::Install) EnableWindow(g.cancel, FALSE);  // ponto sem volta: a troca termina
        if (!g.cancelFlag) SetWindowTextW(g.status, (std::to_wstring(int(p->fraction * 100)) + L"% - " + p->text).c_str());
        return 0;
    }
    case WM_BUILD_DONE: onDone(reinterpret_cast<Result*>(lp)); return 0;
    case WM_CLOSE:
        if (g.running) {  // cancela e fecha quando a thread terminar (nada fica pela metade)
            if (!g.autoMode &&
                MessageBoxW(w, L"A compilação está em andamento. Cancelar e sair?", kTitle, MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            g.cancelFlag = true;
            g.closeAfter = true;
            SetWindowTextW(g.status, L"Cancelando...");
            return 0;
        }
        DestroyWindow(w);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    int       argc = 0;
    wchar_t** wv   = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(wv, wv + argc);
    LocalFree(wv);
    std::wstring disc, out;
    loadPrefs(disc, out);
    g.kit = exeDir() / L"kit";
    for (size_t i = 1; i < args.size(); ++i) {
        const bool more = i + 1 < args.size();
        if (args[i] == L"--disc" && more) disc = args[++i];
        else if (args[i] == L"--out" && more) out = args[++i];
        else if (args[i] == L"--kit" && more) g.kit = args[++i];
        else if (args[i] == L"--sdl3" && more) g.sdl3 = args[++i];
        else if (args[i] == L"--auto") g.autoMode = true;
    }
    if (out.empty()) out = defaultOut().native();
    if (const std::string k = check_kit(g.kit, g.sdl3.empty() ? g.kit / L"third_party" / L"SDL3" : g.sdl3); !k.empty()) {
        if (!g.autoMode)
            MessageBoxW(nullptr, (widen(k) + L"\n\nO Builder precisa da pasta \"kit\" ao lado do executável. Extraia de novo a distribuição completa.").c_str(),
                        kTitle, MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"BlueStingerBuilder";
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    const UINT dpi = GetDpiForSystem();
    HWND w = CreateWindowExW(0, wc.lpszClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             MulDiv(720, int(dpi), 96), MulDiv(560, int(dpi), 96), nullptr, nullptr, inst, nullptr);
    if (!w) return 1;
    SetWindowTextW(g.disc, disc.c_str());
    SetWindowTextW(g.out, out.c_str());
    ShowWindow(w, g.autoMode ? SW_SHOWMINNOACTIVE : show);
    if (g.autoMode) PostMessageW(w, WM_COMMAND, ID_BUILD, 0);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(w, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (g.worker.joinable()) g.worker.join();
    if (g.font) DeleteObject(g.font);
    CoUninitialize();
    return g.exitCode;
}
