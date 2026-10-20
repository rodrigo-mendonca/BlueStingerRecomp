// Blue Stinger Developer (janela Win32): a ferramenta de desenvolvimento, em tres areas sobre o mesmo
// bs_builder_core do BlueStinger-Recompiler (a tela simples do jogador, main_recompiler.cpp):
//   Exportar projeto   CDI -> valida, extrai e confere os recursos, recompila (SH-4 -> C++20) e exporta o projeto
//                      editavel (Request::exportOnly: sem compilador, compilacao nem instalacao)
//   Compilar projeto   projeto (ou o CDI) -> compila com os mods (Release/Debug, limpo ou incremental, com ou sem
//                      filmes), verifica e instala; abrir pasta e jogar
//   Inspecionar        uma instalacao ou um projeto exportado, sem o CDI (builder/resource_inspect)
// Log tecnico, progresso e cancelamento numa thread. Textos pelo catalogo (builder/ui, chaves dev.*); idioma,
// pastas e area em builder.ini.
//   BlueStinger-Developer.exe [--disc <cdi>] [--out <instalacao>] [--projeto <pasta>] [--tipo Release|Debug]
//                          [--exportar <projeto>] [--recursos <pasta>] [--inspecionar <pasta>]
//                          [--kit <pasta>] [--sdl3 <pasta>] [--idioma <codigo>] [--sem-filmes] [--auto]
//   --auto: comeca sozinho e fecha no fim (0 = sucesso, 1 = falha, 2 = entradas, 3 = cancelado): exporta se
//   --exportar foi dado, senao compila (do projeto, ou do CDI como antes).
#include "builder_core.h"
#include "child_process.h"
#include "resource_inspect.h"
#include "ui/i18n.h"
#include "ui/win_shared.h"

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

#include <toml++/toml.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bs::builder;
using ui::tr;

namespace {

enum : UINT { WM_BUILD_LOG = WM_APP + 1, WM_BUILD_PROGRESS, WM_BUILD_DONE };
enum Area : int { AreaExport = 0, AreaBuild = 1, AreaInspect = 2 };
enum : int {
    ID_LANG = 100, ID_TABS, ID_CANCEL, ID_PROGRESS, ID_STATUS, ID_LOG,
    // exportar
    ID_E_DISC, ID_E_DISC_BR, ID_E_PROJ, ID_E_PROJ_BR, ID_E_RES, ID_E_RES_BR, ID_E_NOMOVIES, ID_E_RUN, ID_E_OPEN,
    // compilar
    ID_B_PROJ, ID_B_PROJ_BR, ID_B_OUT, ID_B_OUT_BR, ID_B_DISC, ID_B_DISC_BR, ID_B_TYPE, ID_B_CLEAN, ID_B_NOMOVIES,
    ID_B_RUN, ID_B_OPEN, ID_B_PLAY,
    // inspecionar
    ID_I_DIR, ID_I_DIR_BR, ID_I_RUN, ID_I_LIST, ID_I_ISSUES
};

struct Progress {
    Step         step;
    double       fraction;
    std::wstring text;
};

struct App {
    HWND wnd = nullptr, langLabel, lang, tabs, cancel, progress, status, log;
    // exportar
    HWND eIntro, eDiscL, eDisc, eDiscBr, eProjL, eProj, eProjBr, eResL, eRes, eResBr, eNoMovies, eRun, eOpen;
    // compilar
    HWND bIntro, bProjL, bProj, bProjBr, bOutL, bOut, bOutBr, bDiscL, bDisc, bDiscBr, bTypeL, bType, bClean, bNoMovies, bRun,
        bOpen, bPlay;
    // inspecionar
    HWND iDirL, iDir, iDirBr, iRun, iSummary, iList, iIssues;
    HFONT             font = nullptr;
    fs::path          kit, sdl3;
    std::thread       worker;
    std::atomic<bool> cancelFlag{false};
    bool              running = false, closeAfter = false, autoMode = false, langFromCli = false;
    Area              area = AreaBuild, runningArea = AreaBuild;
    int               exitCode = 0;
    Result            last;
    ResourceReport    report;
    bool              reportShown = false;
    std::string       statusKey = "dev.status.idle";  // o status guarda chave + parametros: a troca de idioma o retraduz
    std::vector<std::wstring> statusArgs;
} g;

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

// preferencias: %LOCALAPPDATA%\BlueStingerRecomp\builder.ini (pastas, idioma e area, UTF-8)
struct Prefs {
    std::wstring disc, out, proj, res, inspect;
    std::string  lang, type = "Release";
    int          area = AreaBuild;
};

fs::path iniPath() {
    const fs::path base = knownFolder(FOLDERID_LocalAppData);
    return base.empty() ? fs::path() : base / L"BlueStingerRecomp" / L"builder.ini";
}

Prefs loadPrefs() {
    Prefs         p;
    std::ifstream f(iniPath(), std::ios::binary);
    for (std::string l; std::getline(f, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        auto val = [&](const char* k) { return l.rfind(k, 0) == 0 ? std::optional<std::string>(l.substr(std::strlen(k))) : std::nullopt; };
        if (auto v = val("cdi=")) p.disc = widen(*v);
        else if (auto v2 = val("out=")) p.out = widen(*v2);
        else if (auto v3 = val("projeto=")) p.proj = widen(*v3);
        else if (auto v4 = val("recursos=")) p.res = widen(*v4);
        else if (auto v5 = val("inspecionar=")) p.inspect = widen(*v5);
        else if (auto v6 = val("idioma=")) p.lang = *v6;
        else if (auto v7 = val("tipo=")) p.type = *v7 == "Debug" ? "Debug" : "Release";
        else if (auto v8 = val("area=")) p.area = std::clamp(std::atoi(v8->c_str()), 0, 2);
    }
    return p;
}

std::wstring text(HWND h) {
    std::wstring s(size_t(GetWindowTextLengthW(h)), L'\0');
    GetWindowTextW(h, s.data(), int(s.size() + 1));
    return s;
}

bool checked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
std::string buildType() { return SendMessageW(g.bType, CB_GETCURSEL, 0, 0) == 1 ? "Debug" : "Release"; }

void savePrefs() {
    if (g.langFromCli) return;  // --idioma vale so para a execucao
    const fs::path f = iniPath();
    if (f.empty()) return;
    std::error_code ec;
    fs::create_directories(f.parent_path(), ec);
    std::ofstream o(f, std::ios::binary | std::ios::trunc);
    // cdi: o do Exportar (o do Compilar e opcional e fica igual a ele quando vazio)
    const std::wstring disc = text(g.eDisc).empty() ? text(g.bDisc) : text(g.eDisc);
    o << "# Blue Stinger Developer\ncdi=" << narrow(disc) << "\nout=" << narrow(text(g.bOut)) << "\nprojeto="
      << narrow(text(g.bProj).empty() ? text(g.eProj) : text(g.bProj)) << "\nrecursos=" << narrow(text(g.eRes))
      << "\ninspecionar=" << narrow(text(g.iDir)) << "\nidioma=" << ui::current_language().code << "\ntipo=" << buildType()
      << "\narea=" << int(g.area) << "\n";
}

int ask(const std::wstring& msg, UINT icon) { return MessageBoxW(g.wnd, msg.c_str(), tr("dev.title").c_str(), icon); }

void setStatus(std::string key, std::vector<std::wstring> args = {}) {
    g.statusKey  = std::move(key);
    g.statusArgs = std::move(args);
    SetWindowTextW(g.status, tr(g.statusKey, std::span<const std::wstring>(g.statusArgs)).c_str());
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
std::wstring browse(bool folder, const std::wstring& current, const char* titleKey) {
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
    }
    dlg->SetTitle(tr(titleKey).c_str());
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

// pasta dos recursos gravada no projeto (bs-projeto.toml): a instalacao padrao do Compilar (dispensa o CDI)
std::wstring projectResources(const fs::path& proj) {
    try {
        const auto t = toml::parse_file((proj / L"bs-projeto.toml").u8string());
        if (const auto v = t["recursos"].value<std::string>()) return widen(*v);
    } catch (...) {
    }
    return {};
}

void setRunning(bool r) {
    g.running = r;
    for (HWND h : {g.lang, g.eDisc, g.eDiscBr, g.eProj, g.eProjBr, g.eRes, g.eResBr, g.eNoMovies, g.eRun, g.bProj, g.bProjBr,
                   g.bOut, g.bOutBr, g.bDisc, g.bDiscBr, g.bType, g.bClean, g.bNoMovies, g.bRun, g.iRun})
        EnableWindow(h, !r);
    EnableWindow(g.tabs, !r);
    EnableWindow(g.cancel, r);
    const bool done = !r && g.last.ok;
    EnableWindow(g.eOpen, done && !g.last.projectDir.empty());
    EnableWindow(g.bOpen, done && !g.last.exe.empty());
    EnableWindow(g.bPlay, done && !g.last.exe.empty());
}

// Aviso antes de tocar numa instalacao existente, como no Recompiler (nunca no --auto). O Developer nao
// escolhe o modo dos saves: o instalacao.toml da instalacao e preservado.
bool confirmInstall(const fs::path& outDir) {
    const InstallInfo i = inspect_install(outDir);
    if (!i.exists || g.autoMode) return true;
    if (!i.ours) return ask(tr("msg.foreign", {i.dir.native(), i.previous.native()}), MB_YESNO | MB_ICONWARNING) == IDYES;
    return ask(tr("msg.existing", {i.dir.native(), i.previous.native()}), MB_YESNO | MB_ICONQUESTION) == IDYES;
}

void runWorker(Request req) {
    savePrefs();
    SetWindowTextW(g.log, L"");
    SendMessageW(g.progress, PBM_SETPOS, 0, 0);
    g.cancelFlag  = false;
    g.last        = {};
    g.runningArea = g.area;
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

bool inputError(const char* key) {
    if (g.autoMode) {
        g.exitCode = 2;
        DestroyWindow(g.wnd);
        return true;
    }
    ask(tr(key), MB_ICONWARNING);
    return true;
}

void startExport() {
    Request req;
    req.disc          = text(g.eDisc);
    req.outDir        = text(g.eRes);
    req.exportProject = text(g.eProj);
    req.exportOnly    = true;
    req.kitDir        = g.kit;
    req.sdl3Dir       = g.sdl3;
    req.movies        = !checked(g.eNoMovies);
    if (req.disc.empty() || req.outDir.empty() || req.exportProject.empty()) {
        inputError("dev.msg.missing_export");
        return;
    }
    runWorker(std::move(req));
}

void startBuild() {
    Request req;
    req.disc      = text(g.bDisc);
    req.outDir    = text(g.bOut);
    req.kitDir    = g.kit;
    req.sdl3Dir   = g.sdl3;
    req.clean     = checked(g.bClean);
    req.movies    = !checked(g.bNoMovies);  // desmarcado (padrao): converte e instala os filmes
    req.buildType = buildType();
    if (!text(g.bProj).empty()) req.fromProject = text(g.bProj);
    if ((req.disc.empty() && req.fromProject.empty()) || req.outDir.empty()) {
        inputError("dev.msg.missing_build");
        return;
    }
    if (!confirmInstall(req.outDir)) return;
    runWorker(std::move(req));
}

void onDone(Result* r) {
    std::unique_ptr<Result> res(r);
    g.last = *res;
    if (g.worker.joinable()) g.worker.join();
    setRunning(false);
    if (res->ok) {
        SendMessageW(g.progress, PBM_SETPOS, 1000, 0);
        g.exitCode = 0;
        if (g.runningArea == AreaExport) {
            setStatus("dev.status.exported", {res->projectDir.native()});
            // o Compilar ja aponta para o projeto e para a pasta dos recursos (sem o CDI)
            SetWindowTextW(g.bProj, res->projectDir.c_str());
            SetWindowTextW(g.bOut, text(g.eRes).c_str());
            if (!g.autoMode && !g.closeAfter) ask(tr("dev.msg.exported", {res->projectDir.native(), text(g.eRes)}), MB_ICONINFORMATION);
        } else {
            setStatus("dev.status.done");
            if (!g.autoMode && !g.closeAfter) ask(tr("dev.msg.done", {res->installDir.native()}), MB_ICONINFORMATION);
        }
        savePrefs();
    } else {
        g.exitCode = res->cancelled ? 3 : 1;
        if (res->cancelled) setStatus("status.cancelled");
        else setStatus("dev.status.failed", {tr(ui::step_key(res->failedStep))});
        if (!g.autoMode && !g.closeAfter)
            ask(tr("dev.msg.failed", {text(g.status), widen(res->error), res->logFile.native()}), res->cancelled ? MB_ICONINFORMATION : MB_ICONERROR);
    }
    if (g.autoMode || g.closeAfter) DestroyWindow(g.wnd);
}

// ---- inspecao (area propria, so leitura) ----
const char* kindKey(ResourceKind k) {
    switch (k) {
    case ResourceKind::Movie: return "dev.kind.movie";
    case ResourceKind::Audio: return "dev.kind.audio";
    case ResourceKind::Functions: return "dev.kind.functions";
    case ResourceKind::Generated: return "dev.kind.generated";
    case ResourceKind::Mod: return "dev.kind.mod";
    case ResourceKind::Project: return "dev.kind.project";
    default: return "dev.kind.config";
    }
}
const char* stateKey(ResourceState s) {
    switch (s) {
    case ResourceState::Ok: return "dev.state.ok";
    case ResourceState::FileMissing: return "dev.state.missing";
    case ResourceState::Absent: return "dev.state.absent";
    case ResourceState::NotInstalled: return "dev.state.notinstalled";
    case ResourceState::Unverified: return "dev.state.unverified";
    default: return "dev.state.pending";
    }
}
const char* originKey(ResourceOrigin o) {
    switch (o) {
    case ResourceOrigin::Builder: return "dev.origin.builder";
    case ResourceOrigin::Mod: return "dev.origin.mod";
    case ResourceOrigin::Installation: return "dev.origin.install";
    default: return "dev.origin.none";
    }
}
const char* issueKey(InspectProblem p) {
    switch (p) {
    case InspectProblem::NoInstallation: return "dev.issue.noinstall";
    case InspectProblem::NoExecutable: return "dev.issue.noexe";
    case InspectProblem::NoData: return "dev.issue.nodata";
    default: return "dev.issue.manifest";
    }
}

void fillInspection() {  // (re)preenche a lista no idioma atual
    ListView_DeleteAllItems(g.iList);
    while (ListView_DeleteColumn(g.iList, 0)) {
    }
    const UINT dpi = GetDpiForWindow(g.wnd);
    const std::pair<const char*, int> cols[] = {{"dev.insp.col.kind", 90},   {"dev.insp.col.id", 40},     {"dev.insp.col.name", 120},
                                                {"dev.insp.col.state", 190}, {"dev.insp.col.origin", 80}, {"dev.insp.col.mod", 50},
                                                {"dev.insp.col.detail", 220}, {"dev.insp.col.file", 300}};
    for (int i = 0; i < int(std::size(cols)); ++i) {
        std::wstring t = tr(cols[i].first);
        LVCOLUMNW    c{LVCF_TEXT | LVCF_WIDTH};
        c.pszText = t.data();
        c.cx      = MulDiv(cols[i].second, int(dpi), 96);
        ListView_InsertColumn(g.iList, i, &c);
    }
    if (!g.reportShown) {
        SetWindowTextW(g.iSummary, L"");
        SetWindowTextW(g.iIssues, L"");
        return;
    }
    int item = 0;
    for (const auto& r : g.report.rows) {
        const std::wstring cells[] = {tr(kindKey(r.kind)),   widen(r.id),       widen(r.name),
                                      tr(stateKey(r.state)), tr(originKey(r.origin)),
                                      tr(r.moddable ? "dev.mod.yes" : "dev.mod.no"), widen(r.detail),
                                      (r.file.empty() ? r.manifest : r.file).native()};
        LVITEMW it{LVIF_TEXT};
        it.iItem   = item;
        it.pszText = const_cast<wchar_t*>(cells[0].c_str());
        ListView_InsertItem(g.iList, &it);
        for (int c = 1; c < int(std::size(cells)); ++c) ListView_SetItemText(g.iList, item, c, const_cast<wchar_t*>(cells[c].c_str()));
        ++item;
    }
    std::wstring issues;
    for (const auto& i : g.report.issues) issues += tr(issueKey(i.kind), {widen(i.detail)}) + L"\r\n";
    SetWindowTextW(g.iIssues, issues.c_str());
    SetWindowTextW(g.iSummary, tr("dev.insp.summary", {g.report.installDir.native(), std::to_wstring(g.report.rows.size()),
                                                       std::to_wstring(g.report.issues.size())})
                                   .c_str());
}

void inspect(const std::wstring& dir) {
    if (dir.empty()) return;
    g.report      = inspect_resources(dir);
    g.reportShown = true;
    fillInspection();
    savePrefs();
}

HWND make(const wchar_t* cls, DWORD style, int id, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, L"", WS_CHILD | style, 0, 0, 10, 10, g.wnd, HMENU(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, WPARAM(g.font), TRUE);
    return h;
}

std::vector<HWND> areaControls(Area a) {
    switch (a) {
    case AreaExport:
        return {g.eIntro, g.eDiscL, g.eDisc, g.eDiscBr, g.eProjL, g.eProj, g.eProjBr, g.eResL, g.eRes, g.eResBr, g.eNoMovies, g.eRun,
                g.eOpen, g.cancel, g.progress, g.status, g.log};
    case AreaBuild:
        return {g.bIntro, g.bProjL, g.bProj, g.bProjBr, g.bOutL, g.bOut, g.bOutBr, g.bDiscL, g.bDisc, g.bDiscBr, g.bTypeL, g.bType,
                g.bClean, g.bNoMovies, g.bRun, g.bOpen, g.bPlay, g.cancel, g.progress, g.status, g.log};
    default: return {g.iDirL, g.iDir, g.iDirBr, g.iRun, g.iSummary, g.iList, g.iIssues};
    }
}

void layout();

void showArea(Area a) {
    g.area = a;
    for (Area x : {AreaExport, AreaBuild, AreaInspect})
        for (HWND h : areaControls(x)) ShowWindow(h, SW_HIDE);
    for (HWND h : areaControls(a)) ShowWindow(h, SW_SHOW);
    TabCtrl_SetCurSel(g.tabs, int(a));
    layout();
}

void layout() {
    RECT rc;
    GetClientRect(g.wnd, &rc);
    const UINT dpi = GetDpiForWindow(g.wnd);
    auto       s   = [dpi](int v) { return MulDiv(v, int(dpi), 96); };
    const int  W = rc.right, H = rc.bottom, m = s(12), btn = s(96), h = s(24), gap = s(8);
    int        y = m;
    MoveWindow(g.langLabel, m, y + s(3), s(70), s(18), TRUE);
    MoveWindow(g.lang, m + s(72), y, s(160), s(200), TRUE);
    y += h + gap;
    MoveWindow(g.tabs, m, y, W - 2 * m, s(26), TRUE);
    y += s(26) + gap;
    auto field = [&](HWND label, HWND edit, HWND br) {
        MoveWindow(label, m, y, W - 2 * m, s(18), TRUE);
        y += s(20);
        MoveWindow(edit, m, y, W - 3 * m - btn + gap, h, TRUE);
        MoveWindow(br, W - m - btn, y, btn, h, TRUE);
        y += h + gap;
    };
    const int top = y;
    if (g.area == AreaInspect) {
        field(g.iDirL, g.iDir, g.iDirBr);
        MoveWindow(g.iRun, m, y, s(170), s(28), TRUE);
        y += s(28) + gap;
        MoveWindow(g.iSummary, m, y, W - 2 * m, s(18), TRUE);
        y += s(20);
        const int issuesH = s(56);
        MoveWindow(g.iList, m, y, W - 2 * m, std::max(s(80), H - y - m - issuesH - gap), TRUE);
        MoveWindow(g.iIssues, m, H - m - issuesH, W - 2 * m, issuesH, TRUE);
        return;
    }
    if (g.area == AreaExport) {
        MoveWindow(g.eIntro, m, y, W - 2 * m, s(34), TRUE);
        y += s(38);
        field(g.eDiscL, g.eDisc, g.eDiscBr);
        field(g.eProjL, g.eProj, g.eProjBr);
        field(g.eResL, g.eRes, g.eResBr);
        MoveWindow(g.eNoMovies, m, y, W - 2 * m, h, TRUE);
        y += h + gap;
        const int bw = s(170);
        MoveWindow(g.eRun, m, y, bw, s(30), TRUE);
        MoveWindow(g.cancel, m + bw + gap, y, btn, s(30), TRUE);
        MoveWindow(g.eOpen, W - m - btn, y, btn, s(30), TRUE);
        y += s(30) + gap;
    } else {
        MoveWindow(g.bIntro, m, y, W - 2 * m, s(34), TRUE);
        y += s(38);
        field(g.bProjL, g.bProj, g.bProjBr);
        field(g.bOutL, g.bOut, g.bOutBr);
        field(g.bDiscL, g.bDisc, g.bDiscBr);
        MoveWindow(g.bTypeL, m, y + s(3), s(50), s(18), TRUE);
        MoveWindow(g.bType, m + s(52), y, s(300), s(200), TRUE);
        y += h + gap;
        MoveWindow(g.bClean, m, y, W - 2 * m, h, TRUE);
        y += h;
        MoveWindow(g.bNoMovies, m, y, W - 2 * m, h, TRUE);
        y += h + gap;
        const int bw = s(200);
        MoveWindow(g.bRun, m, y, bw, s(30), TRUE);
        MoveWindow(g.cancel, m + bw + gap, y, btn, s(30), TRUE);
        MoveWindow(g.bPlay, W - m - btn, y, btn, s(30), TRUE);
        MoveWindow(g.bOpen, W - m - 2 * btn - gap, y, btn, s(30), TRUE);
        y += s(30) + gap;
    }
    (void)top;
    MoveWindow(g.progress, m, y, W - 2 * m, s(18), TRUE);
    y += s(18) + s(4);
    MoveWindow(g.status, m, y, W - 2 * m, s(18), TRUE);
    y += s(18) + s(4);
    MoveWindow(g.log, m, y, W - 2 * m, std::max(H - y - m, s(40)), TRUE);
}

// Todos os textos da janela a partir do catalogo (inicio e troca de idioma).
void applyLanguage() {
    SetWindowTextW(g.wnd, tr("dev.title").c_str());
    const std::pair<HWND, const char*> texts[] = {
        {g.langLabel, "lang.label"},      {g.cancel, "btn.cancel"},
        {g.eIntro, "dev.exp.intro"},      {g.eDiscL, "dev.disc.label"},  {g.eDiscBr, "browse"},       {g.eProjL, "dev.exp.project"},
        {g.eProjBr, "browse"},            {g.eResL, "dev.exp.resources"}, {g.eResBr, "browse"},       {g.eNoMovies, "dev.exp.nomovies"},
        {g.eRun, "dev.exp.run"},          {g.eOpen, "dev.exp.open"},
        {g.bIntro, "dev.bld.intro"},      {g.bProjL, "dev.bld.project"}, {g.bProjBr, "browse"},       {g.bOutL, "dev.bld.out"},
        {g.bOutBr, "browse"},             {g.bDiscL, "dev.bld.disc"},    {g.bDiscBr, "browse"},       {g.bTypeL, "dev.bld.type"},
        {g.bClean, "dev.clean"},          {g.bNoMovies, "dev.nomovies"}, {g.bRun, "dev.bld.run"},     {g.bOpen, "btn.open"},
        {g.bPlay, "btn.play"},
        {g.iDirL, "dev.insp.dir"},        {g.iDirBr, "browse"},          {g.iRun, "dev.insp.run"}};
    for (const auto& [hw, k] : texts) SetWindowTextW(hw, tr(k).c_str());
    const char* tabKeys[] = {"dev.tab.export", "dev.tab.build", "dev.tab.inspect"};
    for (int i = 0; i < 3; ++i) {
        std::wstring t = tr(tabKeys[i]);
        TCITEMW      it{TCIF_TEXT};
        it.pszText = t.data();
        if (TabCtrl_GetItemCount(g.tabs) > i) TabCtrl_SetItem(g.tabs, i, &it);
        else TabCtrl_InsertItem(g.tabs, i, &it);
    }
    const LRESULT sel = SendMessageW(g.bType, CB_GETCURSEL, 0, 0);
    SendMessageW(g.bType, CB_RESETCONTENT, 0, 0);
    SendMessageW(g.bType, CB_ADDSTRING, 0, LPARAM(tr("dev.bld.release").c_str()));
    SendMessageW(g.bType, CB_ADDSTRING, 0, LPARAM(tr("dev.bld.debug").c_str()));
    SendMessageW(g.bType, CB_SETCURSEL, sel == 1 ? 1 : 0, 0);
    SetWindowTextW(g.status, tr(g.statusKey, std::span<const std::wstring>(g.statusArgs)).c_str());
    fillInspection();
    layout();
}

void createFont() {
    if (g.font) DeleteObject(g.font);
    NONCLIENTMETRICSW ncm{sizeof ncm};
    const UINT        dpi = GetDpiForWindow(g.wnd);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
}

void browseInto(HWND edit, bool folder, const char* titleKey) {
    const std::wstring p = browse(folder, text(edit), titleKey);
    if (!p.empty()) SetWindowTextW(edit, p.c_str());
}

LRESULT CALLBACK wndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.wnd = w;
        createFont();
        const DWORD edit = WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, btn = WS_TABSTOP, chk = WS_TABSTOP | BS_AUTOCHECKBOX;
        g.langLabel = make(L"STATIC", WS_VISIBLE, -1);
        g.lang      = make(WC_COMBOBOXW, WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_LANG);
        g.tabs      = make(WC_TABCONTROLW, WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, ID_TABS);
        // exportar
        g.eIntro    = make(L"STATIC", 0, -1);
        g.eDiscL    = make(L"STATIC", 0, -1);
        g.eDisc     = make(L"EDIT", edit, ID_E_DISC);
        g.eDiscBr   = make(L"BUTTON", btn, ID_E_DISC_BR);
        g.eProjL    = make(L"STATIC", 0, -1);
        g.eProj     = make(L"EDIT", edit, ID_E_PROJ);
        g.eProjBr   = make(L"BUTTON", btn, ID_E_PROJ_BR);
        g.eResL     = make(L"STATIC", 0, -1);
        g.eRes      = make(L"EDIT", edit, ID_E_RES);
        g.eResBr    = make(L"BUTTON", btn, ID_E_RES_BR);
        g.eNoMovies = make(L"BUTTON", chk, ID_E_NOMOVIES);
        g.eRun      = make(L"BUTTON", btn | BS_DEFPUSHBUTTON, ID_E_RUN);
        g.eOpen     = make(L"BUTTON", btn, ID_E_OPEN);
        // compilar
        g.bIntro    = make(L"STATIC", 0, -1);
        g.bProjL    = make(L"STATIC", 0, -1);
        g.bProj     = make(L"EDIT", edit, ID_B_PROJ);
        g.bProjBr   = make(L"BUTTON", btn, ID_B_PROJ_BR);
        g.bOutL     = make(L"STATIC", 0, -1);
        g.bOut      = make(L"EDIT", edit, ID_B_OUT);
        g.bOutBr    = make(L"BUTTON", btn, ID_B_OUT_BR);
        g.bDiscL    = make(L"STATIC", 0, -1);
        g.bDisc     = make(L"EDIT", edit, ID_B_DISC);
        g.bDiscBr   = make(L"BUTTON", btn, ID_B_DISC_BR);
        g.bTypeL    = make(L"STATIC", 0, -1);
        g.bType     = make(WC_COMBOBOXW, WS_TABSTOP | CBS_DROPDOWNLIST, ID_B_TYPE);
        g.bClean    = make(L"BUTTON", chk, ID_B_CLEAN);
        g.bNoMovies = make(L"BUTTON", chk, ID_B_NOMOVIES);  // desmarcado por padrao
        g.bRun      = make(L"BUTTON", btn | BS_DEFPUSHBUTTON, ID_B_RUN);
        g.bOpen     = make(L"BUTTON", btn, ID_B_OPEN);
        g.bPlay     = make(L"BUTTON", btn, ID_B_PLAY);
        // inspecionar
        g.iDirL    = make(L"STATIC", 0, -1);
        g.iDir     = make(L"EDIT", edit, ID_I_DIR);
        g.iDirBr   = make(L"BUTTON", btn, ID_I_DIR_BR);
        g.iRun     = make(L"BUTTON", btn | BS_DEFPUSHBUTTON, ID_I_RUN);
        g.iSummary = make(L"STATIC", SS_ENDELLIPSIS, -1);
        g.iList    = make(WC_LISTVIEWW, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, ID_I_LIST, WS_EX_CLIENTEDGE);
        g.iIssues  = make(L"EDIT", ES_MULTILINE | ES_READONLY | WS_VSCROLL, ID_I_ISSUES, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(g.iList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        // comuns (exportar e compilar)
        g.cancel   = make(L"BUTTON", btn, ID_CANCEL);
        g.progress = make(PROGRESS_CLASSW, 0, ID_PROGRESS);
        SendMessageW(g.progress, PBM_SETRANGE32, 0, 1000);
        g.status = make(L"STATIC", SS_ENDELLIPSIS, ID_STATUS);
        g.log    = make(L"EDIT", WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_BORDER, ID_LOG);
        SendMessageW(g.log, EM_SETLIMITTEXT, 4000000, 0);
        for (const ui::Language* l : ui::languages()) {
            const LRESULT i = SendMessageW(g.lang, CB_ADDSTRING, 0, LPARAM(l->name));
            if (std::string_view(l->code) == ui::current_language().code) SendMessageW(g.lang, CB_SETCURSEL, WPARAM(i), 0);
        }
        setRunning(false);
        applyLanguage();
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
        mm->ptMinTrackSize = {MulDiv(640, int(dpi), 96), MulDiv(560, int(dpi), 96)};
        return 0;
    }
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lp)->idFrom == ID_TABS && reinterpret_cast<NMHDR*>(lp)->code == TCN_SELCHANGE) {
            showArea(Area(TabCtrl_GetCurSel(g.tabs)));
            savePrefs();
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_LANG:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                const LRESULT i = SendMessageW(g.lang, CB_GETCURSEL, 0, 0);
                if (i >= 0 && size_t(i) < ui::languages().size()) {
                    ui::set_language(ui::languages()[size_t(i)]->code);
                    g.langFromCli = false;  // escolha na janela: vira a preferencia
                    applyLanguage();
                    savePrefs();
                }
            }
            break;
        case ID_E_DISC_BR: browseInto(g.eDisc, false, "dlg.disc_title"); break;
        case ID_E_PROJ_BR: browseInto(g.eProj, true, "dev.dlg.project_title"); break;
        case ID_E_RES_BR: browseInto(g.eRes, true, "dev.dlg.resources_title"); break;
        case ID_B_DISC_BR: browseInto(g.bDisc, false, "dlg.disc_title"); break;
        case ID_B_OUT_BR: browseInto(g.bOut, true, "dlg.out_title"); break;
        case ID_B_PROJ_BR: {
            browseInto(g.bProj, true, "dev.dlg.project_title");
            // a pasta dos recursos do projeto vira a instalacao (dispensa o CDI)
            if (const std::wstring res = projectResources(text(g.bProj)); !res.empty()) SetWindowTextW(g.bOut, res.c_str());
            break;
        }
        case ID_I_DIR_BR: browseInto(g.iDir, true, "dev.dlg.inspect_title"); break;
        case ID_I_RUN: inspect(text(g.iDir)); break;
        case ID_E_RUN: if (!g.running) startExport(); break;
        case ID_B_RUN: if (!g.running) startBuild(); break;
        case ID_CANCEL:
            g.cancelFlag = true;
            setStatus("status.cancelling");
            break;
        case ID_E_OPEN: ShellExecuteW(w, L"open", g.last.projectDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case ID_B_OPEN: ShellExecuteW(w, L"open", g.last.installDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case ID_B_PLAY:  // o jogo instalado usa os proprios dados (data\): sem o CDI
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
        // a etapa pelo catalogo; o detalhe tecnico da etapa vai junto (Developer)
        if (!g.cancelFlag) setStatus("dev.status.progress", {std::to_wstring(int(p->fraction * 100)), tr(ui::step_key(p->step)) + L": " + p->text});
        return 0;
    }
    case WM_BUILD_DONE: onDone(reinterpret_cast<Result*>(lp)); return 0;
    case WM_CLOSE:
        if (g.running) {  // cancela e fecha quando a thread terminar (nada fica pela metade)
            if (!g.autoMode && ask(tr("dev.msg.close_running"), MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            g.cancelFlag = true;
            g.closeAfter = true;
            setStatus("status.cancelling");
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
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&icc);

    int       argc = 0;
    wchar_t** wv   = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(wv, wv + argc);
    LocalFree(wv);
    Prefs        prefs = loadPrefs();
    std::string  cliLang;
    bool         noMovies = false, cliExport = false, cliInspect = false, cliArea = false;
    g.kit = exeDir() / L"kit";
    for (size_t i = 1; i < args.size(); ++i) {
        const bool more = i + 1 < args.size();
        if (args[i] == L"--disc" && more) prefs.disc = args[++i];
        else if (args[i] == L"--out" && more) prefs.out = args[++i], prefs.area = AreaBuild, cliArea = true;
        else if (args[i] == L"--projeto" && more) prefs.proj = args[++i], prefs.area = AreaBuild, cliArea = true;
        else if (args[i] == L"--exportar" && more) prefs.proj = args[++i], prefs.area = AreaExport, cliExport = cliArea = true;
        else if (args[i] == L"--recursos" && more) prefs.res = args[++i];
        else if (args[i] == L"--inspecionar" && more) prefs.inspect = args[++i], prefs.area = AreaInspect, cliInspect = cliArea = true;
        else if (args[i] == L"--tipo" && more) prefs.type = narrow(args[++i]) == "Debug" ? "Debug" : "Release";
        else if (args[i] == L"--kit" && more) g.kit = args[++i];
        else if (args[i] == L"--sdl3" && more) g.sdl3 = args[++i];
        else if (args[i] == L"--idioma" && more) cliLang = narrow(args[++i]), g.langFromCli = true;
        else if (args[i] == L"--auto") g.autoMode = true;
        else if (args[i] == L"--sem-filmes") noMovies = true;
    }
    if (g.autoMode && !cliArea) prefs.area = AreaBuild;  // --auto sem area: compila (o comportamento de antes)
    ui::set_language(ui::initial_language(g.langFromCli ? cliLang : prefs.lang, ui::windows_languages()));
    const fs::path docs = knownFolder(FOLDERID_Documents);
    if (prefs.out.empty()) prefs.out = (docs / L"Blue Stinger Recompilado").native();
    if (prefs.res.empty()) prefs.res = prefs.out;
    if (prefs.proj.empty()) prefs.proj = (docs / L"Blue Stinger Projeto").native();
    if (const std::string k = check_kit(g.kit, g.sdl3.empty() ? g.kit / L"third_party" / L"SDL3" : g.sdl3); !k.empty()) {
        if (!g.autoMode) MessageBoxW(nullptr, tr("dev.msg.kit_missing", {widen(k)}).c_str(), tr("dev.title").c_str(), MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"BlueStingerDeveloper";
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    const UINT dpi = GetDpiForSystem();
    HWND w = CreateWindowExW(0, wc.lpszClassName, tr("dev.title").c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             MulDiv(820, int(dpi), 96), MulDiv(700, int(dpi), 96), nullptr, nullptr, inst, nullptr);
    if (!w) return 1;
    SetWindowTextW(g.eDisc, prefs.disc.c_str());
    SetWindowTextW(g.bDisc, prefs.disc.c_str());
    SetWindowTextW(g.eProj, prefs.proj.c_str());
    SetWindowTextW(g.eRes, prefs.res.c_str());
    SetWindowTextW(g.bOut, prefs.out.c_str());
    SetWindowTextW(g.iDir, prefs.inspect.c_str());
    // Compilar: um projeto so quando pedido (--projeto) ou ja usado; senao compila do CDI (como antes)
    if (cliArea && prefs.area == AreaBuild) {
        bool proj = false;
        for (size_t i = 1; i < args.size(); ++i) proj |= args[i] == L"--projeto";
        SetWindowTextW(g.bProj, proj ? prefs.proj.c_str() : L"");
    }
    SendMessageW(g.bType, CB_SETCURSEL, prefs.type == "Debug" ? 1 : 0, 0);
    SendMessageW(g.eNoMovies, BM_SETCHECK, noMovies ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.bNoMovies, BM_SETCHECK, noMovies ? BST_CHECKED : BST_UNCHECKED, 0);
    showArea(Area(prefs.area));
    ShowWindow(w, g.autoMode ? SW_SHOWMINNOACTIVE : show);
    if (cliInspect) inspect(prefs.inspect);
    if (g.autoMode) PostMessageW(w, WM_COMMAND, cliExport ? ID_E_RUN : ID_B_RUN, 0);

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
