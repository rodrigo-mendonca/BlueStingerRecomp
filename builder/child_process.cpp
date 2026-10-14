#include "child_process.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <vector>

namespace bs::builder {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int    n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int   n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string path_text(const std::filesystem::path& p) { return narrow(p.native()); }

std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    // regras do CommandLineToArgvW: barras antes de aspas (e antes da aspa final) dobram
    std::wstring r = L"\"";
    size_t       slashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') r.append(slashes * 2 + 1, L'\\');
        else r.append(slashes, L'\\');
        slashes = 0;
        r += c;
    }
    r.append(slashes * 2, L'\\');
    r += L'"';
    return r;
}

bool ansi_safe(const std::filesystem::path& p) {
    const std::wstring& w = p.native();
    if (w.empty()) return true;
    BOOL usedDefault = FALSE;
    const int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.data(), int(w.size()), nullptr, 0, nullptr,
                                      &usedDefault);
    return n > 0 && !usedDefault;
}

std::string error_text(const std::error_code& ec) {
    if (ec.category() == std::system_category()) {
        wchar_t*    buf = nullptr;
        const DWORD n   = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                             FORMAT_MESSAGE_IGNORE_INSERTS,
                                         nullptr, DWORD(ec.value()), 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
        std::wstring w = n ? std::wstring(buf, n) : std::wstring();
        if (buf) LocalFree(buf);
        while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L' ' || w.back() == L'.'))
            w.pop_back();
        if (!w.empty()) return narrow(w) + " (" + std::to_string(ec.value()) + ")";
    }
    const std::string m = ec.message();
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m.data(), int(m.size()), nullptr, 0) > 0) return m;
    const int    n = MultiByteToWideChar(CP_ACP, 0, m.data(), int(m.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, m.data(), int(m.size()), w.data(), n);
    return narrow(w);
}

Env current_env() {
    Env       env;
    wchar_t*  block = GetEnvironmentStringsW();
    if (!block) return env;
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
        const std::wstring e = p;
        const size_t       eq = e.find(L'=', 1);  // nomes como "=C:" comecam com '='
        if (eq != std::wstring::npos) env[e.substr(0, eq)] = e.substr(eq + 1);
    }
    FreeEnvironmentStringsW(block);
    return env;
}

namespace {

bool validUtf8(const std::string& s) {
    return s.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0) > 0;
}

std::string lineText(const std::string& raw) {
    if (validUtf8(raw)) return raw;
    const int    n = MultiByteToWideChar(CP_OEMCP, 0, raw.data(), int(raw.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, raw.data(), int(raw.size()), w.data(), n);
    return narrow(w);
}

// Bloco de ambiente UTF-16: "nome=valor\0...\0\0", ordenado sem diferenciar maiusculas.
std::vector<wchar_t> envBlock(const Env& env) {
    std::vector<std::pair<std::wstring, std::wstring>> v(env.begin(), env.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        return CompareStringOrdinal(a.first.c_str(), int(a.first.size()), b.first.c_str(), int(b.first.size()),
                                    TRUE) == CSTR_LESS_THAN;
    });
    std::vector<wchar_t> b;
    for (const auto& [k, val] : v) {
        b.insert(b.end(), k.begin(), k.end());
        b.push_back(L'=');
        b.insert(b.end(), val.begin(), val.end());
        b.push_back(L'\0');
    }
    b.push_back(L'\0');
    if (b.size() == 1) b.push_back(L'\0');
    return b;
}

struct Handle {
    HANDLE h = nullptr;
    ~Handle() { reset(); }
    void reset(HANDLE n = nullptr) {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = n;
    }
};

class LineSplitter {
public:
    LineSplitter(const std::function<void(const std::string&)>& f, bool utf16) : f_(f), utf16_(utf16) {}
    void feed(const char* p, size_t n) {
        buf_.append(p, n);
        if (utf16_) {
            const size_t even = buf_.size() & ~size_t(1);
            std::wstring w(reinterpret_cast<const wchar_t*>(buf_.data()), even / 2);
            buf_.erase(0, even);
            text_ += narrow(w);
        } else {
            text_ += buf_;
            buf_.clear();
        }
        size_t pos;
        while ((pos = text_.find('\n')) != std::string::npos) {
            emit(text_.substr(0, pos));
            text_.erase(0, pos + 1);
        }
    }
    void finish() {
        if (!text_.empty()) emit(text_);
        text_.clear();
    }

private:
    void emit(std::string l) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (f_) f_(utf16_ ? l : lineText(l));
    }
    const std::function<void(const std::string&)>& f_;
    bool                                           utf16_;
    std::string                                    buf_, text_;
};

} // namespace

ProcessResult run_process(const std::wstring& commandLine, const std::filesystem::path& cwd, const Env& env,
                          const std::function<void(const std::string&)>& onLine, const std::atomic<bool>* cancel,
                          bool utf16Output, unsigned timeoutMs) {
    ProcessResult res;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    Handle              rd, wr, nul, job, proc;
    if (!CreatePipe(&rd.h, &wr.h, &sa, 0)) return res;
    SetHandleInformation(rd.h, HANDLE_FLAG_INHERIT, 0);
    nul.reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));

    // so os tres handles passam para o filho (outras threads do Builder podem criar processos)
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto*             attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (nul.h == INVALID_HANDLE_VALUE || !InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) return res;
    HANDLE inherit[2] = {wr.h, nul.h};
    if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attrs);
        return res;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb         = sizeof si;
    si.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput  = nul.h;
    si.StartupInfo.hStdOutput = wr.h;
    si.StartupInfo.hStdError  = wr.h;
    si.lpAttributeList        = attrs;

    // sem job valido nao ha como cancelar a arvore: nao inicia (revisao Codex 2026-10-14)
    job.reset(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.h || !SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &lim, sizeof lim)) {
        DeleteProcThreadAttributeList(attrs);
        return res;
    }

    std::vector<wchar_t> cmd(commandLine.begin(), commandLine.end());
    cmd.push_back(L'\0');
    std::vector<wchar_t> block;
    if (!env.empty()) block = envBlock(env);
    PROCESS_INFORMATION pi{};
    const std::wstring  dir = cwd.empty() ? std::wstring() : cwd.native();
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED |
                                       EXTENDED_STARTUPINFO_PRESENT,
                                   block.empty() ? nullptr : block.data(), dir.empty() ? nullptr : dir.c_str(),
                                   &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    wr.reset();  // o pipe fecha quando o filho (e quem herdou dele) terminar
    if (!ok) return res;
    proc.reset(pi.hProcess);
    // o filho so roda depois de estar no job (os netos herdam o job)
    if (!AssignProcessToJobObject(job.h, pi.hProcess) || ResumeThread(pi.hThread) == DWORD(-1)) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        return res;
    }
    CloseHandle(pi.hThread);
    res.started = true;

    LineSplitter     lines(onLine, utf16Output);
    char             buf[8192];
    const ULONGLONG  t0     = GetTickCount64();
    bool             exited = false, killed = false;
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(rd.h, nullptr, 0, nullptr, &avail, nullptr) && avail) {
            DWORD got = 0;
            if (ReadFile(rd.h, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr) && got) lines.feed(buf, got);
            continue;
        }
        if (exited) break;  // saida do filho drenada; netos que herdaram o pipe nao seguram o Builder
        if (!killed) {
            if (cancel && cancel->load()) res.cancelled = true;
            else if (timeoutMs && GetTickCount64() - t0 > timeoutMs) res.timedOut = true;
            if (res.cancelled || res.timedOut) {
                // a arvore inteira; se o job falhar, ao menos o filho direto
                if (!TerminateJobObject(job.h, 1)) TerminateProcess(proc.h, 1);
                killed = true;
            }
        }
        exited = WaitForSingleObject(proc.h, 15) == WAIT_OBJECT_0;
    }
    if (killed) TerminateJobObject(job.h, 1);  // netos criados depois da primeira tentativa
    lines.finish();
    DWORD code = DWORD(-1);
    GetExitCodeProcess(proc.h, &code);
    res.exitCode = int(code);
    return res;  // fechar o job mata o que sobrou da arvore (KILL_ON_JOB_CLOSE)
}

} // namespace bs::builder
