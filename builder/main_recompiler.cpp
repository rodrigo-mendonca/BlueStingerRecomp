// Blue Stinger Recompiler (janela Win32): a tela simples para o usuario final. Escolhe o CDI, a pasta
// de instalacao, o idioma da janela e o modo portatil, e roda o mesmo fluxo do builder_core que o
// BlueStinger-Developer.exe (sem as opcoes avancadas nem o log tecnico, que fica no builder.log).
// Todos os textos vem do catalogo (builder/ui/i18n.h); trocar o idioma retraduz a janela na hora.
//   BlueStinger-Recompiler.exe [--disc <cdi>] [--out <pasta>] [--kit <pasta>] [--sdl3 <pasta>]
//                              [--portatil] [--idioma <codigo>] [--auto] [--sem-filmes]
//   --auto: comeca sozinho, sem dialogos, e fecha no fim (codigo 0 = sucesso, 1 = falha, 2 = entradas,
//   3 = cancelado); --idioma vale so para esta execucao.
#include "builder_core.h"
#include "child_process.h"
#include "ui/i18n.h"
#include "ui/win_shared.h"

#ifndef UNICODE
#define UNICODE
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
using ui::tr;

namespace {

enum : UINT { WM_BUILD_PROGRESS = WM_APP + 1, WM_BUILD_DONE };
enum : int {
    ID_LANG = 100, ID_HELP, ID_DISC, ID_DISC_BROWSE, ID_OUT, ID_OUT_BROWSE, ID_PORTABLE, ID_BUILD, ID_CANCEL, ID_OPEN,
    ID_PLAY, ID_PROGRESS, ID_STATUS
};

struct Progress {
    Step   step;
    double fraction;
};

// O status guarda a chave e os parametros: trocar o idioma o retraduz.
struct Status {
    std::string               key = "status.idle";
    std::vector<std::wstring> args;
    std::string               stepKey;  // etapa (status.progress / status.failed)
    int                       percent = 0;
};

struct App {
    HWND              wnd = nullptr;
    HWND              langLabel, lang, help, intro, discLabel, disc, discBrowse, outLabel, out, outBrowse, portable,
                      portableHint, build, cancel, open, play, progress, status, tips;
    HFONT             font = nullptr;
    fs::path          kit, sdl3;
    std::thread       worker;
    std::atomic<bool> cancelFlag{false};
    bool              running = false, closeAfter = false, autoMode = false, langFromCli = false, noMovies = false;
    int               exitCode = 0;
    Result            last;
    Status            st;
} g;

const char* stepKey(Step s) { return ui::step_key(s); }

fs::path exeDir() {
    std::wstring b(32768, L'\0');
    b.resize(GetModuleFileNameW(nullptr, b.data(), DWORD(b.size())));
    return fs::path(b).parent_path();
}

fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR    p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

// preferencias: %LOCALAPPDATA%\BlueStingerRecomp\recompiler.ini (ultima imagem, pasta e idioma, UTF-8)
struct Prefs {
    std::wstring disc, out;
    std::string  lang;
};

fs::path iniPath() {
    const fs::path base = knownFolder(FOLDERID_LocalAppData);
    return base.empty() ? fs::path() : base / L"BlueStingerRecomp" / L"recompiler.ini";
}

Prefs loadPrefs() {
    Prefs         p;
    std::ifstream f(iniPath(), std::ios::binary);
    for (std::string l; std::getline(f, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.rfind("cdi=", 0) == 0) p.disc = widen(l.substr(4));
        else if (l.rfind("out=", 0) == 0) p.out = widen(l.substr(4));
        else if (l.rfind("idioma=", 0) == 0) p.lang = l.substr(7);
    }
    return p;
}

void savePrefs(const Prefs& p) {
    const fs::path f = iniPath();
    if (f.empty()) return;
    std::error_code ec;
    fs::create_directories(f.parent_path(), ec);
    std::ofstream o(f, std::ios::binary | std::ios::trunc);
    o << "# Blue Stinger Recompiler\ncdi=" << narrow(p.disc) << "\nout=" << narrow(p.out) << "\nidioma=" << p.lang << "\n";
}

std::wstring text(HWND h) {
    std::wstring s(size_t(GetWindowTextLengthW(h)), L'\0');
    GetWindowTextW(h, s.data(), int(s.size() + 1));
    return s;
}

Prefs currentPrefs() { return {text(g.disc), text(g.out), ui::current_language().code}; }

std::vector<std::wstring> windowsLanguages() { return ui::windows_languages(); }

bool checked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }

int ask(const std::wstring& msg, UINT icon) { return MessageBoxW(g.wnd, msg.c_str(), tr("app.title").c_str(), icon); }

void renderStatus() {
    std::wstring s;
    if (g.st.key == "status.progress") s = tr("status.progress", {std::to_wstring(g.st.percent), tr(g.st.stepKey)});
    else if (g.st.key == "status.failed") s = tr("status.failed", {tr(g.st.stepKey)});
    else s = tr(g.st.key, std::span<const std::wstring>(g.st.args));
    SetWindowTextW(g.status, s.c_str());
}

void setStatus(std::string key, std::vector<std::wstring> args = {}) {
    g.st.key  = std::move(key);
    g.st.args = std::move(args);
    renderStatus();
}

void setTip(HWND ctl, const char* key) {
    TOOLINFOW ti{sizeof ti};
    ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd     = g.wnd;
    ti.uId      = UINT_PTR(ctl);
    const std::wstring t = tr(key);
    ti.lpszText = const_cast<wchar_t*>(t.c_str());
    if (!SendMessageW(g.tips, TTM_UPDATETIPTEXTW, 0, LPARAM(&ti))) SendMessageW(g.tips, TTM_ADDTOOLW, 0, LPARAM(&ti));
    SendMessageW(g.tips, TTM_UPDATETIPTEXTW, 0, LPARAM(&ti));
}

void layout();

// Todos os textos da janela a partir do catalogo (inicio e troca de idioma).
void applyLanguage() {
    SetWindowTextW(g.wnd, tr("app.title").c_str());
    const std::pair<HWND, const char*> texts[] = {
        {g.langLabel, "lang.label"}, {g.help, "btn.help"},       {g.intro, "app.intro"},       {g.discLabel, "disc.label"},
        {g.discBrowse, "browse"},    {g.outLabel, "out.label"},  {g.outBrowse, "browse"},      {g.portable, "portable.check"},
        {g.build, "btn.build"},      {g.cancel, "btn.cancel"},   {g.open, "btn.open"},         {g.play, "btn.play"}};
    for (const auto& [h, k] : texts) SetWindowTextW(h, tr(k).c_str());
    SetWindowTextW(g.portableHint, tr(checked(g.portable) ? "portable.on" : "portable.off").c_str());
    setTip(g.lang, "tip.lang");
    setTip(g.disc, "tip.disc");
    setTip(g.out, "tip.out");
    setTip(g.portable, "tip.portable");
    setTip(g.build, "tip.build");
    renderStatus();
    layout();
}

std::wstring browse(bool folder, const std::wstring& current) {
    std::wstring     result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    const std::wstring cdi = tr("dlg.cdi_filter"), all = tr("dlg.all_files");
    if (!folder) {
        const COMDLG_FILTERSPEC spec[] = {{cdi.c_str(), L"*.cdi"}, {all.c_str(), L"*.*"}};
        dlg->SetFileTypes(2, spec);
        dlg->SetTitle(tr("dlg.disc_title").c_str());
    } else {
        dlg->SetTitle(tr("dlg.out_title").c_str());
    }
    {  // comeca na escolha atual; sem ela, na pasta do proprio Recompiler
        std::error_code ec;
        fs::path        start = current.empty() ? exeDir() : folder ? fs::path(current) : fs::path(current).parent_path();
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
    for (HWND h : {g.disc, g.discBrowse, g.out, g.outBrowse, g.portable, g.build}) EnableWindow(h, !r);
    EnableWindow(g.cancel, r);
    const bool done = !r && g.last.ok;
    EnableWindow(g.open, done);
    EnableWindow(g.play, done);
}

// Avisos antes de tocar numa instalacao existente (nunca no --auto).
bool confirmInstall(const fs::path& outDir, bool portable) {
    const InstallInfo i = inspect_install(outDir);
    if (!i.exists || g.autoMode) return true;
    if (!i.ours) return ask(tr("msg.foreign", {i.dir.native(), i.previous.native()}), MB_YESNO | MB_ICONWARNING) == IDYES;
    if (ask(tr("msg.existing", {i.dir.native(), i.previous.native()}), MB_YESNO | MB_ICONQUESTION) != IDYES) return false;
    if (i.portable != portable) {
        const std::wstring from = tr(i.portable ? "loc.portable" : "loc.documents"), to = tr(portable ? "loc.portable" : "loc.documents");
        return ask(tr("msg.mode_change", {from, to}), MB_YESNO | MB_ICONWARNING) == IDYES;
    }
    return true;
}

void startBuild() {
    Request req;
    req.disc          = text(g.disc);
    req.outDir        = text(g.out);
    req.kitDir        = g.kit;
    req.sdl3Dir       = g.sdl3;
    req.portableSaves = checked(g.portable);
    req.movies        = !g.noMovies;
    if (req.disc.empty() || req.outDir.empty()) {
        if (g.autoMode) {
            g.exitCode = 2;
            DestroyWindow(g.wnd);
            return;
        }
        ask(tr("msg.missing_input"), MB_ICONWARNING);
        return;
    }
    if (!confirmInstall(req.outDir, *req.portableSaves)) return;
    if (!g.langFromCli) savePrefs(currentPrefs());
    SendMessageW(g.progress, PBM_SETPOS, 0, 0);
    g.cancelFlag = false;
    g.last       = {};
    setRunning(true);
    g.st.stepKey = stepKey(Step::Validate), g.st.percent = 0;
    setStatus("status.progress");
    if (g.worker.joinable()) g.worker.join();
    g.worker = std::thread([req] {
        Callbacks cb;
        cb.cancel   = &g.cancelFlag;
        cb.progress = [](Step s, double f, const std::string&) {  // a mensagem tecnica fica so no builder.log
            PostMessageW(g.wnd, WM_BUILD_PROGRESS, 0, LPARAM(new Progress{s, f}));
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
    const bool quiet = g.autoMode || g.closeAfter;
    if (res->ok) {
        SendMessageW(g.progress, PBM_SETPOS, 1000, 0);
        setStatus("status.done", {res->installDir.native()});
        g.exitCode = 0;
        if (!quiet) {
            std::wstring m = tr("msg.success", {res->installDir.native()});
            if (!res->warning.empty()) m += tr("msg.previous_not_archived", {res->logFile.native()});
            ask(m, res->warning.empty() ? MB_ICONINFORMATION : MB_ICONWARNING);
        }
    } else if (res->cancelled) {
        g.exitCode = 3;
        setStatus("status.cancelled");
        if (!quiet) ask(tr("status.cancelled"), MB_ICONINFORMATION);
    } else {
        g.exitCode   = 1;
        g.st.stepKey = stepKey(res->failedStep);
        setStatus("status.failed");
        if (!quiet) ask(tr(error_key(res->kind)) + tr("err.footer", {res->logFile.native()}), MB_ICONERROR);
    }
    if (quiet) DestroyWindow(g.wnd);
}

HWND make(const wchar_t* cls, DWORD style, int id, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.wnd, HMENU(INT_PTR(id)),
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, WPARAM(g.font), TRUE);
    return h;
}

void layout() {
    if (!g.wnd || !g.status) return;
    RECT rc;
    GetClientRect(g.wnd, &rc);
    const UINT dpi = GetDpiForWindow(g.wnd);
    auto       s   = [dpi](int v) { return MulDiv(v, int(dpi), 96); };
    const int  W = rc.right, m = s(12), btn = s(110), h = s(24), gap = s(8);
    int        y = m;
    // idioma e ajuda no topo, a direita
    const int comboW = s(190);
    MoveWindow(g.help, W - m - btn, y, btn, h, TRUE);
    MoveWindow(g.lang, W - m - btn - gap - comboW, y, comboW, s(200), TRUE);  // altura = lista aberta
    MoveWindow(g.langLabel, m, y + s(4), W - 3 * m - btn - comboW - gap, s(18), TRUE);
    y += h + gap;
    MoveWindow(g.intro, m, y, W - 2 * m, s(18), TRUE);
    y += s(18) + gap;
    MoveWindow(g.discLabel, m, y, W - 2 * m, s(18), TRUE);
    y += s(20);
    MoveWindow(g.disc, m, y, W - 2 * m - btn - gap, h, TRUE);
    MoveWindow(g.discBrowse, W - m - btn, y, btn, h, TRUE);
    y += h + gap;
    MoveWindow(g.outLabel, m, y, W - 2 * m, s(18), TRUE);
    y += s(20);
    MoveWindow(g.out, m, y, W - 2 * m - btn - gap, h, TRUE);
    MoveWindow(g.outBrowse, W - m - btn, y, btn, h, TRUE);
    y += h + gap;
    MoveWindow(g.portable, m, y, W - 2 * m, h, TRUE);
    y += h;
    MoveWindow(g.portableHint, m + s(18), y, W - 2 * m - s(18), s(34), TRUE);
    y += s(34) + gap;
    const int bw = s(150);
    MoveWindow(g.build, m, y, bw, s(30), TRUE);
    MoveWindow(g.cancel, m + bw + gap, y, btn, s(30), TRUE);
    MoveWindow(g.play, W - m - btn, y, btn, s(30), TRUE);
    MoveWindow(g.open, W - m - 2 * btn - gap, y, btn, s(30), TRUE);
    y += s(30) + gap;
    MoveWindow(g.progress, m, y, W - 2 * m, s(18), TRUE);
    y += s(18) + s(4);
    MoveWindow(g.status, m, y, W - 2 * m, s(36), TRUE);
}

void createFont() {
    if (g.font) DeleteObject(g.font);
    NONCLIENTMETRICSW ncm{sizeof ncm};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, GetDpiForWindow(g.wnd));
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
}

void showHelp() { MessageBoxW(g.wnd, tr("help.body").c_str(), tr("help.title").c_str(), MB_ICONINFORMATION); }

void cancelBuild() {
    if (!g.running) return;
    g.cancelFlag = true;
    setStatus("status.cancelling");
}

LRESULT CALLBACK wndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.wnd = w;
        createFont();
        g.langLabel = make(L"STATIC", SS_RIGHT, -1);
        g.lang      = make(WC_COMBOBOXW, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_LANG);
        for (const ui::Language* l : ui::languages()) SendMessageW(g.lang, CB_ADDSTRING, 0, LPARAM(l->name));
        g.help         = make(L"BUTTON", WS_TABSTOP, ID_HELP);
        g.intro        = make(L"STATIC", 0, -1);
        g.discLabel    = make(L"STATIC", 0, -1);
        g.disc         = make(L"EDIT", WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, ID_DISC);
        g.discBrowse   = make(L"BUTTON", WS_TABSTOP, ID_DISC_BROWSE);
        g.outLabel     = make(L"STATIC", 0, -1);
        g.out          = make(L"EDIT", WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, ID_OUT);
        g.outBrowse    = make(L"BUTTON", WS_TABSTOP, ID_OUT_BROWSE);
        g.portable     = make(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX, ID_PORTABLE);  // desmarcado por padrao
        g.portableHint = make(L"STATIC", 0, -1);
        g.build        = make(L"BUTTON", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_BUILD);
        g.cancel       = make(L"BUTTON", WS_TABSTOP, ID_CANCEL);
        g.open         = make(L"BUTTON", WS_TABSTOP, ID_OPEN);
        g.play         = make(L"BUTTON", WS_TABSTOP, ID_PLAY);
        g.progress     = make(PROGRESS_CLASSW, 0, ID_PROGRESS);
        SendMessageW(g.progress, PBM_SETRANGE32, 0, 1000);
        g.status = make(L"STATIC", SS_ENDELLIPSIS, ID_STATUS);
        g.tips   = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, w, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g.tips, TTM_SETMAXTIPWIDTH, 0, 400);
        setRunning(false);
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
        auto*      mm  = reinterpret_cast<MINMAXINFO*>(lp);
        const UINT dpi = g.wnd ? GetDpiForWindow(g.wnd) : 96;
        mm->ptMinTrackSize = {MulDiv(600, int(dpi), 96), MulDiv(400, int(dpi), 96)};
        return 0;
    }
    case WM_HELP: showHelp(); return TRUE;  // F1
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_LANG:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                const LRESULT i = SendMessageW(g.lang, CB_GETCURSEL, 0, 0);
                if (i >= 0 && size_t(i) < ui::languages().size()) {
                    ui::set_language(ui::languages()[size_t(i)]->code);
                    g.langFromCli = false;  // escolha do usuario: vale para as proximas execucoes
                    savePrefs(currentPrefs());
                    applyLanguage();
                }
            }
            break;
        case ID_HELP: showHelp(); break;
        case ID_PORTABLE: SetWindowTextW(g.portableHint, tr(checked(g.portable) ? "portable.on" : "portable.off").c_str()); break;
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
        case IDOK:  // Enter
        case ID_BUILD:
            if (!g.running) startBuild();
            break;
        case IDCANCEL:  // Esc
        case ID_CANCEL: cancelBuild(); break;
        case ID_OPEN: ShellExecuteW(w, L"open", g.last.installDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case ID_PLAY:  // o jogo instalado le instalacao.toml e os proprios dados (data\): sem o CDI
            ShellExecuteW(w, L"open", g.last.exe.c_str(), nullptr, g.last.installDir.c_str(), SW_SHOWNORMAL);
            break;
        }
        return 0;
    case WM_BUILD_PROGRESS: {
        std::unique_ptr<Progress> p(reinterpret_cast<Progress*>(lp));
        SendMessageW(g.progress, PBM_SETPOS, WPARAM(p->fraction * 1000), 0);
        if (p->step == Step::Install) EnableWindow(g.cancel, FALSE);  // ponto sem volta: a troca termina
        if (!g.cancelFlag) {
            g.st.stepKey = stepKey(p->step), g.st.percent = int(p->fraction * 100);
            setStatus("status.progress");
        }
        return 0;
    }
    case WM_BUILD_DONE: onDone(reinterpret_cast<Result*>(lp)); return 0;
    case WM_CLOSE:
        if (g.running) {  // cancela e fecha quando a thread terminar (nada fica pela metade)
            if (!g.autoMode && ask(tr("msg.close_running"), MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            g.closeAfter = true;
            cancelBuild();
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
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    int       argc = 0;
    wchar_t** wv   = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(wv, wv + argc);
    LocalFree(wv);
    Prefs prefs = loadPrefs();
    bool  portable = false;  // padrao obrigatorio: desmarcado
    std::string cliLang;
    bool        discFromCli = false;
    g.kit = exeDir() / L"kit";
    for (size_t i = 1; i < args.size(); ++i) {
        const bool more = i + 1 < args.size();
        if (args[i] == L"--disc" && more) prefs.disc = args[++i], discFromCli = true;
        else if (args[i] == L"--out" && more) prefs.out = args[++i];
        else if (args[i] == L"--kit" && more) g.kit = args[++i];
        else if (args[i] == L"--sdl3" && more) g.sdl3 = args[++i];
        else if (args[i] == L"--portatil") portable = true;
        else if (args[i] == L"--idioma" && more) cliLang = narrow(args[++i]);
        else if (args[i] == L"--auto") g.autoMode = true;
        else if (args[i] == L"--sem-filmes") g.noMovies = true;  // testes: sem a conversao dos filmes (N5)
    }
    g.langFromCli = !cliLang.empty();
    ui::set_language(ui::initial_language(g.langFromCli ? cliLang : prefs.lang, windowsLanguages()));
    // sem imagem valida (nenhuma, ou a salva sumiu): um .cdi na pasta do Recompiler (o usuario costuma
    // colocar o jogo ao lado). --disc vale como escolhido, mesmo se nao existir (o erro e mostrado).
    if (!discFromCli && (prefs.disc.empty() || !fs::is_regular_file(prefs.disc))) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(exeDir(), ec))
            if (e.is_regular_file(ec) && _wcsicmp(e.path().extension().c_str(), L".cdi") == 0) {
                prefs.disc = e.path().native();
                break;
            }
    }
    if (prefs.out.empty()) {
        const fs::path docs = knownFolder(FOLDERID_Documents);
        if (!docs.empty()) prefs.out = (docs / L"Blue Stinger Recompilado").native();
    }
    if (const std::string k = check_kit(g.kit, g.sdl3.empty() ? g.kit / L"third_party" / L"SDL3" : g.sdl3); !k.empty()) {
        if (!g.autoMode) MessageBoxW(nullptr, tr("msg.kit_missing").c_str(), tr("app.title").c_str(), MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"BlueStingerRecompiler";
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    const UINT dpi = GetDpiForSystem();
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             MulDiv(680, int(dpi), 96), MulDiv(440, int(dpi), 96), nullptr, nullptr, inst, nullptr);
    if (!w) return 1;
    SetWindowTextW(g.disc, prefs.disc.c_str());
    SetWindowTextW(g.out, prefs.out.c_str());
    SendMessageW(g.portable, BM_SETCHECK, portable ? BST_CHECKED : BST_UNCHECKED, 0);
    for (size_t i = 0; i < ui::languages().size(); ++i)
        if (ui::languages()[i] == &ui::current_language()) SendMessageW(g.lang, CB_SETCURSEL, i, 0);
    applyLanguage();
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
