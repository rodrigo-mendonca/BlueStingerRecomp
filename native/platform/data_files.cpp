#include "platform/data_files.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace bs::native::files {

namespace fs = std::filesystem;

namespace {

std::string utf8(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

std::FILE* openRead(const fs::path& p) {
#ifdef _WIN32
    std::FILE* f = nullptr;
    return _wfopen_s(&f, p.c_str(), L"rb") == 0 ? f : nullptr;
#else
    return std::fopen(p.c_str(), "rb");
#endif
}

// Percorre `dir` e devolve (nome logico canonico -> arquivo).
std::map<std::string, std::pair<fs::path, uint64_t>> walk(const fs::path& dir, std::vector<std::string>* problems) {
    std::map<std::string, std::pair<fs::path, uint64_t>> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& e = *it;
        std::error_code            ec2;
        if (e.is_directory(ec2)) continue;
        const std::string rel = utf8(fs::relative(e.path(), dir, ec2));
        if (ec2) {
            problems->push_back(utf8(e.path()) + ": " + ec2.message());
            continue;
        }
        if (!e.is_regular_file(ec2)) {
            problems->push_back(rel + ": nao e um arquivo comum");
            continue;
        }
        const uint64_t size = e.file_size(ec2);
        if (ec2) {
            problems->push_back(rel + ": " + ec2.message());
            continue;
        }
        out[DataFileSystem::normalize(rel)] = {e.path(), size};
    }
    if (ec) problems->push_back(utf8(dir) + ": " + ec.message());
    return out;
}

} // namespace

std::string DataFileSystem::normalize(std::string_view path) { return canonical_path(path); }

std::string canonical_path(std::string_view path) {
    std::string s;
    s.reserve(path.size());
    for (char ch : path) s += ch == '\\' ? '/' : char(std::toupper(static_cast<unsigned char>(ch)));
    if (const size_t semi = s.rfind(';'); semi != std::string::npos) s.resize(semi);  // versao ISO ";1"
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    // "a//b" e "a/./b" -> "a/b"
    std::string out;
    size_t      i = 0;
    while (i <= s.size()) {
        const size_t      j    = std::min(s.find('/', i), s.size());
        const std::string part = s.substr(i, j - i);
        if (!part.empty() && part != ".") out += (out.empty() ? "" : "/") + part;
        i = j + 1;
    }
    return out;
}

DataFileSystem::DataFileSystem(const fs::path& root, const fs::path& mods) {
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        throw DataFilesError("pasta de recursos do jogo ausente: " + utf8(root) +
                             ". Gere a instalacao de novo com o Builder.");
    std::vector<std::string> baseProblems;  // erro ao percorrer a pasta base: indice incompleto
    for (auto& [name, v] : walk(root, &baseProblems)) index_[name] = {v.first, v.second, name};
    if (!baseProblems.empty()) {
        std::string msg = "pasta de recursos do jogo com problemas (" + utf8(root) + "):";
        for (const auto& p : baseProblems) msg += "\n  " + p;
        throw DataFilesError(msg + "\nGere a instalacao de novo com o Builder.");
    }
    if (index_.empty()) throw DataFilesError("pasta de recursos do jogo vazia: " + utf8(root));
    if (mods.empty() || !fs::exists(mods, ec)) return;

    std::vector<std::string> problems;
    for (auto& [name, v] : walk(mods, &problems)) {
        // documentacao na raiz da pasta de mods (o projeto exportado traz um LEIA-ME.txt): nenhum
        // recurso do jogo mora ali com esse nome; ignorada
        if (name.find('/') == std::string::npos && (name.rfind("LEIA-ME", 0) == 0 || name.rfind("README", 0) == 0))
            continue;
        const auto it = index_.find(name);
        if (it == index_.end()) {
            problems.push_back(name + ": nao substitui nenhum recurso do jogo (confira o caminho e o nome)");
            continue;
        }
        std::FILE* f = openRead(v.first);
        if (!f) {
            problems.push_back(name + ": nao pode ser lido");
            continue;
        }
        std::fclose(f);
        mods_.push_back({name, it->second.size, v.second});
        it->second.path = v.first;
        it->second.size = v.second;
    }
    if (!problems.empty()) {
        std::string msg = "mods de recursos invalidos em " + utf8(mods) + ":";
        for (const auto& p : problems) msg += "\n  " + p;
        throw DataFilesError(msg);
    }
}

DataFileSystem::~DataFileSystem() {
    for (auto& [h, o] : open_)
        if (o.f) std::fclose(o.f);
}

Result<FileHandle> DataFileSystem::open(std::string_view path) {
    const auto it = index_.find(normalize(path));
    if (it == index_.end()) return Error::NotFound;
    std::FILE* f = openRead(it->second.path);
    if (!f) return Error::Io;
    const uint32_t h = nextHandle_++;
    open_[h]         = {&it->second, f};
    return FileHandle{h};
}

Result<uint64_t> DataFileSystem::size(FileHandle f) const {
    const auto it = open_.find(f.value);
    if (it == open_.end()) return Error::InvalidHandle;
    return it->second.entry->size;
}

Result<size_t> DataFileSystem::read(FileHandle f, uint64_t offset, std::span<std::byte> out) {
    const auto it = open_.find(f.value);
    if (it == open_.end()) return Error::InvalidHandle;
    const uint64_t size = it->second.entry->size;
    if (offset > size) return Error::OutOfRange;
    const size_t n = size_t(std::min<uint64_t>(out.size(), size - offset));
    if (!n) return size_t(0);
    if (_fseeki64(it->second.f, int64_t(offset), SEEK_SET) != 0) return Error::Io;
    if (std::fread(out.data(), 1, n, it->second.f) != n) return Error::Io;
    return n;
}

void DataFileSystem::close(FileHandle f) {
    const auto it = open_.find(f.value);
    if (it == open_.end()) return;
    if (it->second.f) std::fclose(it->second.f);
    open_.erase(it);
}

// Leitura assincrona: os arquivos sao locais; o pedido termina na propria chamada e o
// estado "concluido" fica disponivel para a consulta seguinte.
Result<RequestId> DataFileSystem::read_async(FileHandle f, uint64_t offset, std::span<std::byte> out) {
    const auto r = read(f, offset, out);
    if (!r) return r.error();
    const uint32_t id = nextRequest_++;
    requests_[id]     = {RequestState::Done, r.value(), Error::None};
    return RequestId{id};
}

RequestStatus DataFileSystem::poll(RequestId r) {
    const auto it = requests_.find(r.value);
    if (it == requests_.end()) return {RequestState::Failed, 0, Error::InvalidHandle};
    const RequestStatus s = it->second;
    requests_.erase(it);
    return s;
}

void DataFileSystem::cancel(RequestId r) { requests_.erase(r.value); }

Result<std::vector<DirEntry>> DataFileSystem::list(std::string_view directory) const {
    std::string dir = normalize(directory);
    if (!dir.empty()) dir += '/';
    std::vector<DirEntry> out;
    std::string           lastDir;
    for (auto it = index_.lower_bound(dir); it != index_.end() && it->first.rfind(dir, 0) == 0; ++it) {
        const std::string rest  = it->first.substr(dir.size());
        const size_t      slash = rest.find('/');
        if (slash == std::string::npos) out.push_back({rest, it->second.size, false});
        else if (rest.substr(0, slash) != lastDir) out.push_back({lastDir = rest.substr(0, slash), 0, true});
    }
    if (out.empty() && !dir.empty()) return Error::NotFound;
    return out;
}

std::string DataFileSystem::logical_name(FileHandle f) const {
    const auto it = open_.find(f.value);
    return it == open_.end() ? std::string() : it->second.entry->logical;
}

} // namespace bs::native::files
