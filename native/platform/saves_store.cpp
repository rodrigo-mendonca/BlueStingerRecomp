// N4: saves em disco, escrita atomica no Windows (docs/N4-PREPARACAO.md, "Escrita atomica").
//   1. envelope inteiro em <NOME>.bsv.tmp na mesma pasta (CreateFileW, WriteFile, FlushFileBuffers);
//   2. releitura e conferencia do temporario;
//   3. troca: MoveFileExW (WRITE_THROUGH) sem destino; ReplaceFileW com .bak com destino;
//   4. so entao None para o chamador (o adaptador so conclui para o jogo depois disso).
#include "platform/saves.h"

#include <windows.h>

#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

namespace bs::native::saves {

namespace fs = std::filesystem;

namespace {

bool readAll(const fs::path& p, std::vector<std::byte>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n < 0) return false;
    out.resize(size_t(n));
    f.seekg(0);
    return bool(f.read(reinterpret_cast<char*>(out.data()), n)) || n == 0;
}

std::string narrowPath(const fs::path& p) {
    const std::wstring w = p.wstring();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

class FileStore final : public ISaveStore {
public:
    FileStore(fs::path root, FileStoreOptions o) : root_(std::move(root)), o_(std::move(o)) {
        if (!o_.now) o_.now = [] { return int64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); };
        std::error_code ec;
        for (int u = 0; u < o_.units; ++u) fs::create_directories(dir(u), ec);  // cartao formatado e vazio
    }

    SaveError card(int unit, CardState& out) override {
        out = {};
        if (const SaveError e = open(unit); e != SaveError::None) return e;
        out.present = out.formatted = true;
        out.total_blocks = kCardBlocks;
        out.total_entries = kCardEntries;
        uint32_t used = 0, files = 0;
        for (const auto& [name, blocks] : scan(unit)) used += blocks, ++files;
        out.free_blocks = used >= kCardBlocks ? 0 : kCardBlocks - used;
        out.free_entries = files >= kCardEntries ? 0 : kCardEntries - files;
        return SaveError::None;
    }

    SaveError list(int unit, std::vector<SaveEntry>& out) override {
        out.clear();
        if (const SaveError e = open(unit); e != SaveError::None) return e;
        for (const auto& [name, blocks] : scan(unit)) {
            std::vector<std::byte> f, img;
            int64_t                t = 0;
            if (readAll(file(unit, name, L".bsv"), f) && envelope_decode(f, img, &t) == SaveError::None)
                out.push_back({name, blocks, t});
        }
        return SaveError::None;
    }

    SaveError load(int unit, std::string_view name, std::vector<std::byte>& image) override {
        if (const SaveError e = open(unit); e != SaveError::None) return e;
        if (!valid_save_name(name)) return SaveError::BadName;
        std::vector<std::byte> f;
        const fs::path         p = file(unit, name, L".bsv");
        std::error_code        ec;
        if (!fs::exists(p, ec)) return SaveError::NotFound;
        if (!readAll(p, f)) return SaveError::Io;
        return envelope_decode(f, image);
    }

    SaveError save_atomic(int unit, std::string_view name, std::span<const std::byte> image) override {
        if (const SaveError e = open(unit); e != SaveError::None) return e;
        if (!valid_save_name(name)) return SaveError::BadName;
        if (image.empty() || image.size() % kBlock || image.size() > size_t(kCardBlocks) * kBlock) return SaveError::BadImage;
        // espaco: o arquivo antigo de mesmo nome sai na troca
        CardState c;
        card(unit, c);
        const auto     files = scan(unit);
        const auto     old   = files.find(std::string(name));
        const uint32_t need  = uint32_t(image.size() / kBlock);
        const uint32_t avail = c.free_blocks + (old != files.end() ? old->second : 0);
        if (need > avail) return SaveError::NoSpace;
        if (old == files.end() && c.free_entries == 0) return SaveError::NoEntries;

        const fs::path dst = file(unit, name, L".bsv"), tmp = file(unit, name, L".bsv.tmp"), bak = file(unit, name, L".bsv.bak");
        std::error_code ec;
        if (fs::exists(dst, ec) && status(dst) == SaveError::FutureVersion) return SaveError::FutureVersion;  // nunca por cima
        const auto env = envelope_encode(image, o_.now());
        const SaveError w = writeDurable(tmp, env);
        if (w != SaveError::None) {
            fs::remove(tmp, ec);
            return w;
        }
        std::vector<std::byte> back, img;
        if (fault(FaultPoint::Reread) || !readAll(tmp, back) || back != env || envelope_decode(back, img) != SaveError::None) {
            fs::remove(tmp, ec);
            return SaveError::Io;
        }
        if (fault(FaultPoint::Swap) || !swap(dst, tmp, bak)) {
            fs::remove(tmp, ec);
            return SaveError::Io;
        }
        return SaveError::None;
    }

    SaveError remove(int unit, std::string_view name) override {
        if (const SaveError e = open(unit); e != SaveError::None) return e;
        if (!valid_save_name(name)) return SaveError::BadName;
        std::error_code ec;
        const fs::path  p = file(unit, name, L".bsv");
        if (!fs::exists(p, ec)) return SaveError::NotFound;
        if (!fs::remove(p, ec)) return SaveError::Io;
        fs::remove(file(unit, name, L".bsv.bak"), ec);
        return SaveError::None;
    }

private:
    fs::path dir(int unit) const { return root_ / (L"card" + std::to_wstring(unit)); }
    fs::path file(int unit, std::string_view name, const wchar_t* ext) const {
        return dir(unit) / (std::wstring(name.begin(), name.end()) + ext);
    }
    bool fault(FaultPoint p) const { return o_.fault && o_.fault(p); }
    void log(const std::string& m) const { if (o_.log) o_.log(m); }

    SaveError open(int unit) {
        if (unit < 0 || unit >= o_.units) return SaveError::NoCard;
        std::error_code ec;
        if (!fs::is_directory(dir(unit), ec)) return SaveError::NoCard;
        if (!recovered_.count(unit)) recover(unit), recovered_.insert(unit);
        return SaveError::None;
    }

    // nome -> blocos dos destinos validos
    std::map<std::string, uint32_t> scan(int unit) const {
        std::map<std::string, uint32_t> m;
        std::error_code                 ec;
        for (const auto& e : fs::directory_iterator(dir(unit), ec)) {
            if (!e.is_regular_file(ec) || e.path().extension() != L".bsv") continue;
            const std::string n = narrowPath(e.path().stem());
            if (!valid_save_name(n)) continue;
            std::vector<std::byte> f, img;
            if (!readAll(e.path(), f)) continue;
            const SaveError st = envelope_decode(f, img);
            if (st == SaveError::None) m[n] = uint32_t(img.size() / kBlock);
            else if (st == SaveError::FutureVersion) m[n] = uint32_t((f.size() + kBlock - 1) / kBlock);  // ocupa espaco
        }
        return m;
    }

    SaveError status(const fs::path& p) const {
        std::vector<std::byte> f, img;
        return readAll(p, f) ? envelope_decode(f, img) : SaveError::Io;
    }
    bool validFile(const fs::path& p) const { return status(p) == SaveError::None; }

    void quarantine(const fs::path& p) const {
        std::error_code ec;
        fs::path        q = p;
        q += L".corrompido";
        for (int i = 1; fs::exists(q, ec); ++i) q = p, q += L".corrompido." + std::to_wstring(i);
        fs::rename(p, q, ec);
        log("[saves] arquivo invalido preservado como " + narrowPath(q.filename()));
    }

    // Abertura do cartao: .tmp descartado; destino invalido com .bak valido volta pelo .bak.
    void recover(int unit) {
        std::error_code       ec;
        std::set<std::string> names;
        std::vector<fs::path> temps;
        for (const auto& e : fs::directory_iterator(dir(unit), ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::wstring f = e.path().filename().wstring();
            for (const wchar_t* ext : {L".bsv.tmp", L".bsv.bak", L".bsv"}) {
                const size_t n = wcslen(ext);
                if (f.size() > n && f.compare(f.size() - n, n, ext) == 0) {
                    const std::string base = narrowPath(fs::path(f.substr(0, f.size() - n)));
                    if (!valid_save_name(base)) break;
                    if (std::wstring(ext) == L".bsv.tmp") temps.push_back(e.path());
                    else names.insert(base);
                    break;
                }
            }
        }
        for (const auto& t : temps) {
            fs::remove(t, ec);
            log("[saves] gravacao interrompida descartada: " + narrowPath(t.filename()));
        }
        for (const auto& n : names) {
            const fs::path dst = file(unit, n, L".bsv"), bak = file(unit, n, L".bsv.bak");
            const bool     hasDst = fs::exists(dst, ec), hasBak = fs::exists(bak, ec);
            const SaveError st    = hasDst ? status(dst) : SaveError::NotFound;
            if (st == SaveError::None) continue;
            if (st == SaveError::FutureVersion) {  // de uma versao mais nova do jogo: intocado
                log("[saves] " + n + " e de uma versao mais nova do formato: preservado, sem uso nesta versao");
                continue;
            }
            if (hasBak && validFile(bak)) {
                if (hasDst) quarantine(dst);
                fs::rename(bak, dst, ec);
                log("[saves] " + n + " recuperado pela copia anterior (.bak)" + (ec ? " FALHOU: " + ec.message() : ""));
                continue;
            }
            if (hasDst) quarantine(dst);
            if (hasBak) quarantine(bak);
        }
    }

    SaveError writeDurable(const fs::path& p, const std::vector<std::byte>& data) const {
        if (fault(FaultPoint::CreateTemp)) return SaveError::Io;
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_ACCESS_DENIED ? SaveError::ReadOnly : SaveError::Io;
        bool         ok   = true;
        const size_t half = data.size() / 2;
        // falha de escrita injetada: metade do arquivo no disco (temporario truncado)
        const size_t n = fault(FaultPoint::Write) ? half : data.size();
        DWORD        wrote = 0;
        ok = WriteFile(h, data.data(), DWORD(n), &wrote, nullptr) && wrote == n && n == data.size();
        if (ok && (fault(FaultPoint::Flush) || !FlushFileBuffers(h))) ok = false;
        CloseHandle(h);
        return ok ? SaveError::None : SaveError::Io;
    }

    bool swap(const fs::path& dst, const fs::path& tmp, const fs::path& bak) const {
        std::error_code ec;
        if (!fs::exists(dst, ec)) return MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
        fs::remove(bak, ec);  // a copia anterior da anterior: o destino atual e valido e vira o novo .bak
        if (ReplaceFileW(dst.c_str(), tmp.c_str(), bak.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) return true;
        // ERROR_UNABLE_TO_MOVE_REPLACEMENT_2: o destino ja virou .bak e o temporario nao entrou
        if (!fs::exists(dst, ec) && fs::exists(bak, ec))
            return MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
        return false;
    }

    fs::path              root_;
    FileStoreOptions      o_;
    std::set<int>         recovered_;
};

} // namespace

std::unique_ptr<ISaveStore> make_file_store(fs::path root, FileStoreOptions opt) {
    return std::make_unique<FileStore>(std::move(root), std::move(opt));
}

SaveError export_vms(ISaveStore& s, int unit, std::string_view name, const fs::path& out) {
    std::vector<std::byte> img;
    if (const SaveError e = s.load(unit, name, img); e != SaveError::None) return e;
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
    return f ? SaveError::None : SaveError::Io;
}

SaveError import_vms(ISaveStore& s, int unit, std::string_view name, const fs::path& in) {
    std::vector<std::byte> img;
    std::error_code        ec;
    if (!fs::exists(in, ec)) return SaveError::NotFound;
    if (!readAll(in, img)) return SaveError::Io;
    if (!vms_check(img)) return SaveError::BadImage;  // dado externo: so VMS integro entra
    return s.save_atomic(unit, name, img);
}

} // namespace bs::native::saves
