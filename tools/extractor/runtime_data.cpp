#include "runtime_data.h"

#include "disc/disc_image.h"
#include "disc/ip_bin.h"
#include "disc/iso9660.h"
#include "disc/scramble.h"
#include "known_versions.h"
#include "util/hash.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace bs::disc {
namespace {

constexpr const char* kManifest = "disc.manifest";
constexpr const char* kMetaIdx  = "disc_meta.idx";
constexpr const char* kMetaBin  = "disc_meta.bin";
constexpr uint32_t    kSector   = kUserDataSize;

fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string lowerAscii(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

uint32_t sectorsOf(uint32_t size) { return (size + kSector - 1) / kSector; }

// Caminho vindo do disco: so componentes simples (nada de "..", raiz, unidade ou ':').
bool safeIsoPath(const std::string& p) {
    if (p.empty() || p.front() == '/' || p.find('\\') != std::string::npos || p.find(':') != std::string::npos)
        return false;
    std::istringstream in(p);
    for (std::string part; std::getline(in, part, '/');) {
        if (part.empty() || part == "." || part == "..") return false;
        // nomes de dispositivo do Windows (CON, NUL, COM1...), com ou sem extensao, nao sao arquivos
        const std::string stem = lowerAscii(part.substr(0, part.find('.')));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            ((stem.rfind("com", 0) == 0 || stem.rfind("lpt", 0) == 0) && stem.size() == 4 && stem[3] >= '0' && stem[3] <= '9'))
            return false;
    }
    return true;
}

FILE* openRead(const fs::path& p) {
#ifdef _WIN32
    return _wfopen(p.c_str(), L"rb");
#else
    return std::fopen(p.c_str(), "rb");
#endif
}

bool readAt(FILE* f, uint64_t off, void* dst, size_t n, size_t& got) {
#ifdef _WIN32
    if (_fseeki64(f, int64_t(off), SEEK_SET) != 0) return false;
#else
    if (fseeko(f, off_t(off), SEEK_SET) != 0) return false;
#endif
    got = std::fread(dst, 1, n, f);
    return !std::ferror(f);
}

void checkCancel(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) throw RuntimeDataError("cancelado");
}

std::string fileSha256(const fs::path& p) {
    std::ifstream     in(p, std::ios::binary);
    util::Sha256      sha;
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), std::streamsize(buf.size()));
        sha.update(buf.data(), size_t(in.gcount()));
    }
    return sha.hexdigest();
}

std::string regenerate() {
    return "Gere a instalacao de novo com o Blue Stinger Builder (a partir do seu CDI).";
}

} // namespace

RuntimeManifest load_runtime_manifest(const fs::path& dir) {
    RuntimeManifest m;
    std::ifstream   in(dir / kManifest, std::ios::binary);
    if (!in)
        throw RuntimeDataError("dados do jogo nao encontrados em " + pathText(dir) + " (falta " + kManifest + "). " +
                               regenerate());
    std::string line;
    int         lineNo = 0;
    auto        bad    = [&](const std::string& why) {
        return RuntimeDataError(std::string(kManifest) + " invalido (linha " + std::to_string(lineNo) + ": " + why +
                                "). " + regenerate());
    };
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream s(line);
        std::string        key;
        s >> key;
        if (key == "bs-runtime-data") {
            s >> m.format;
        } else if (key == "version") {
            s >> m.versionId;
        } else if (key == "boot") {
            int sc = 0;
            s >> m.bootFile >> m.bootSha256 >> sc >> m.bootTrackLba;
            m.bootScrambled = sc != 0;
        } else if (key == "meta") {
            s >> m.metaIdxSha256 >> m.metaBinSha256;
        } else if (key == "track") {
            RuntimeTrack t;
            int          data = 0;
            s >> t.number >> t.session >> t.start_lba >> t.length >> data;
            t.data = data != 0;
            m.tracks.push_back(t);
        } else if (key == "file") {
            RuntimeFile f;
            s >> f.lba >> f.size >> f.sha256;
            std::getline(s >> std::ws, f.path);
            if (!safeIsoPath(f.path)) throw bad("caminho invalido");
            m.files.push_back(std::move(f));
        } else {
            throw bad("chave desconhecida " + key);
        }
        if (s.fail()) throw bad("campos faltando");
    }
    if (m.format != kRuntimeDataFormat)
        throw RuntimeDataError("dados do jogo em formato " + std::to_string(m.format) + "; este executavel usa o formato " +
                               std::to_string(kRuntimeDataFormat) + ". " + regenerate());
    if (m.tracks.empty() || m.bootFile.empty() || m.metaIdxSha256.empty() || m.metaBinSha256.empty())
        throw RuntimeDataError(std::string(kManifest) + " incompleto. " + regenerate());

    // presenca e tamanho de cada arquivo (rapido; o conteudo e conferido pelo --verify-data)
    std::vector<std::string> missing;
    size_t                   nMissing = 0;
    std::error_code          ec;
    for (const auto& f : m.files) {
        const fs::path p  = dir / "disc" / u8path(f.path);
        const auto     sz = fs::file_size(p, ec);
        if (ec || sz != f.size) {
            if (missing.size() < 5) missing.push_back(f.path + (ec ? " (ausente)" : " (tamanho diferente)"));
            ++nMissing;
        }
    }
    const auto idx = fs::file_size(dir / kMetaIdx, ec);
    const auto bin = ec ? 0 : fs::file_size(dir / kMetaBin, ec);
    if (ec || idx % 4 != 0 || bin != idx / 4 * kSector) {
        missing.push_back(std::string(kMetaIdx) + "/" + kMetaBin + " (ausente ou inconsistente)");
        ++nMissing;
    }
    if (nMissing) {
        std::string list;
        for (const auto& x : missing) list += "\n  " + x;
        throw RuntimeDataError("dados do jogo incompletos em " + pathText(dir) + ": " + std::to_string(nMissing) +
                               " problema(s), por exemplo:" + list + "\n" + regenerate());
    }
    return m;
}

// --- leitor ---

RuntimeDisc::RuntimeDisc(const fs::path& dir, const fs::path& overrides)
    : dir_(dir), m_(load_runtime_manifest(dir)), overrides_(overrides) {
    overrideSize_.assign(m_.files.size(), 0);
    std::error_code ec;
    if (!overrides_.empty() && fs::is_directory(overrides_, ec)) {
        for (size_t i = 0; i < m_.files.size(); ++i) {
            const fs::path p = (overrides_ / u8path(m_.files[i].path)).make_preferred();
            if (!fs::is_regular_file(p, ec)) continue;
            const uint64_t size = fs::file_size(p, ec);
            const uint64_t room = uint64_t(sectorsOf(m_.files[i].size)) * kSector;
            if (ec || size > room)
                throw RuntimeDataError("mod de arquivo maior que o original: " + pathText(p) + " tem " +
                                       std::to_string(size) + " bytes; cabem ate " + std::to_string(room) +
                                       " (o tamanho no disco nao muda). Reduza o arquivo ou remova o mod.");
            overrideSize_[i] = uint32_t(size) + 1;
            overriddenNames_.push_back(m_.files[i].path);
        }
    }
    {
        std::ifstream idx(dir / kMetaIdx, std::ios::binary);
        uint32_t      v;
        while (idx.read(reinterpret_cast<char*>(&v), 4)) metaLba_.push_back(v);
        if (!std::is_sorted(metaLba_.begin(), metaLba_.end()))
            throw RuntimeDataError(std::string(kMetaIdx) + " fora de ordem. " + regenerate());
    }
    meta_ = openRead(dir / kMetaBin);
    if (!meta_) throw RuntimeDataError("nao foi possivel abrir " + pathText(dir / kMetaBin));
    byLba_.resize(m_.files.size());
    for (size_t i = 0; i < byLba_.size(); ++i) byLba_[i] = i;
    std::sort(byLba_.begin(), byLba_.end(), [&](size_t a, size_t b) {
        const auto& x = m_.files[a];
        const auto& y = m_.files[b];
        return x.lba != y.lba ? x.lba < y.lba : x.size < y.size;
    });
}

RuntimeDisc::~RuntimeDisc() {
    if (meta_) std::fclose(meta_);
    for (auto& o : open_) std::fclose(o.f);
}

FILE* RuntimeDisc::open(size_t index) {
    for (size_t i = 0; i < open_.size(); ++i)
        if (open_[i].index == index) {
            const Open o = open_[i];
            open_.erase(open_.begin() + std::ptrdiff_t(i));
            open_.push_back(o);
            return o.f;
        }
    const fs::path p = overrideSize_[index] ? overrides_ / u8path(m_.files[index].path)
                                            : dir_ / "disc" / u8path(m_.files[index].path);
    FILE*          f = openRead(p);
    if (!f) throw RuntimeDataError("arquivo de dados do jogo ausente: " + pathText(p) + ". " + regenerate());
    if (open_.size() >= 16) {
        std::fclose(open_.front().f);
        open_.erase(open_.begin());
    }
    open_.push_back({index, f});
    return f;
}

bool RuntimeDisc::readSector(uint32_t lba, uint8_t* out) {
    const RuntimeTrack* track = nullptr;
    for (const auto& t : m_.tracks)
        if (lba >= t.start_lba && lba - t.start_lba < t.length) track = &t;
    if (!track || !track->data) return false;

    // arquivo que cobre o setor: o ultimo que comeca ate `lba` (alguns anteriores, caso haja
    // sobreposicao)
    size_t covering = SIZE_MAX;
    auto   it       = std::upper_bound(byLba_.begin(), byLba_.end(), lba,
                                       [&](uint32_t v, size_t i) { return v < m_.files[i].lba; });
    for (int back = 0; back < 8 && it != byLba_.begin() && covering == SIZE_MAX; ++back) {
        --it;
        const RuntimeFile& f = m_.files[*it];
        if (lba - f.lba < sectorsOf(f.size)) covering = *it;
    }
    // setor do sistema (ou sobra nao nula do ultimo setor de um arquivo), exceto quando o
    // arquivo foi substituido por um mod: ai vale o conteudo do mod
    const bool modded = covering != SIZE_MAX && overrideSize_[covering];
    const auto mi     = std::lower_bound(metaLba_.begin(), metaLba_.end(), lba);
    if (!modded && mi != metaLba_.end() && *mi == lba) {
        size_t got = 0;
        if (!readAt(meta_, uint64_t(mi - metaLba_.begin()) * kSector, out, kSector, got) || got != kSector)
            throw RuntimeDataError("falha ao ler " + pathText(dir_ / kMetaBin) + ". " + regenerate());
        return true;
    }
    std::memset(out, 0, kSector);
    if (covering == SIZE_MAX) return true;  // setor de dados sem arquivo nem meta: zeros
    const RuntimeFile& f    = m_.files[covering];
    const uint64_t     size = modded ? overrideSize_[covering] - 1 : f.size;
    const uint64_t     off  = uint64_t(lba - f.lba) * kSector;
    if (off >= size) return true;  // mod menor que o original: completa com zeros
    const size_t want = size_t(std::min<uint64_t>(kSector, size - off));
    size_t       got  = 0;
    if (!readAt(open(covering), off, out, want, got) || got != want)
        throw RuntimeDataError("falha ao ler o arquivo de dados " + f.path + ". " + regenerate());
    return true;
}

bool RuntimeDisc::locate(uint32_t lba, std::string& path, uint64_t& offset, uint64_t& size) const {
    auto it = std::upper_bound(byLba_.begin(), byLba_.end(), lba,
                               [&](uint32_t v, size_t i) { return v < m_.files[i].lba; });
    for (int back = 0; back < 8 && it != byLba_.begin(); ++back) {
        --it;
        const RuntimeFile& f = m_.files[*it];
        if (lba - f.lba < sectorsOf(f.size)) {
            path   = f.path;
            offset = uint64_t(lba - f.lba) * kSector;
            size   = overrideSize_[*it] ? overrideSize_[*it] - 1 : f.size;
            return true;
        }
    }
    return false;
}

std::vector<uint8_t> RuntimeDisc::readFile(const std::string& isoPath) {
    const std::string want = lowerAscii(isoPath);
    for (size_t i = 0; i < m_.files.size(); ++i) {
        if (lowerAscii(m_.files[i].path) != want) continue;
        std::vector<uint8_t> d(overrideSize_[i] ? overrideSize_[i] - 1 : m_.files[i].size);
        size_t               got = 0;
        if (!readAt(open(i), 0, d.data(), d.size(), got) || got != d.size())
            throw RuntimeDataError("falha ao ler o arquivo de dados " + isoPath + ". " + regenerate());
        return d;
    }
    throw RuntimeDataError("arquivo " + isoPath + " nao esta nos dados do jogo. " + regenerate());
}

std::vector<uint8_t> RuntimeDisc::ipBin() {
    std::vector<uint8_t> ip(kIpBinSize);
    for (uint32_t i = 0; i < kIpBinSize / kSector; ++i)
        if (!readSector(m_.bootTrackLba + i, ip.data() + i * kSector))
            throw RuntimeDataError("IP.BIN fora das faixas de dados. " + regenerate());
    return ip;
}

// --- preparacao ---

PrepareResult prepare_runtime_data(const DiscImage& img, const fs::path& dir, const ProgressFn& progress,
                                   const std::atomic<bool>* cancel) {
    PrepareResult res;
    auto          say = [&](double f, const std::string& w) {
        if (progress) progress(f, w);
    };
    const Track& boot = img.bootTrack();
    std::vector<uint8_t> ipbin(kIpBinSize);
    for (uint32_t i = 0; i < kIpBinSize / kSector; ++i) img.readUserData(boot.start_lba + i, ipbin.data() + i * kSector);
    const IpBin ip = parseIpBin(ipbin.data(), ipbin.size());
    Iso9660     iso(img, boot.start_lba + 16);
    const auto* bootEntry = iso.find(ip.boot_file);
    if (!bootEntry || bootEntry->is_dir) throw RuntimeDataError("executavel de boot " + ip.boot_file + " nao encontrado");

    // versao: hash do executavel (como no disco ou desembaralhado)
    const std::vector<uint8_t> raw = iso.readAll(*bootEntry);
    std::string                bootSha;
    bool                       scrambled = false;
    for (int pass = 0; pass < 2 && res.versionId.empty(); ++pass) {
        const std::vector<uint8_t> d = pass ? descramble(raw) : raw;
        const std::string          h = util::sha256Hex(d.data(), d.size());
        for (const auto& v : kKnownVersions)
            if (h == v.boot_sha256) res.versionId = v.id, bootSha = h, scrambled = pass == 1;
    }
    if (res.versionId.empty())
        throw RuntimeDataError("versao nao suportada: " + ip.product + " " + ip.version +
                               " (o executavel de boot nao tem o hash da versao recompilada)");

    std::error_code ec;
    fs::create_directories(dir / "disc", ec);
    if (ec) throw RuntimeDataError("nao foi possivel criar " + pathText(dir) + " (" + ec.message() + ")");

    // faixas e mapa de setores cobertos por arquivos
    uint32_t maxLba = 0;
    for (const auto& t : img.tracks()) maxLba = std::max(maxLba, t.start_lba + t.length);
    std::vector<uint8_t> covered(maxLba, 0);  // 1 = arquivo; 2 = arquivo com sobra nao nula (vai para meta)
    uint64_t             total = 0;
    for (const auto& e : iso.entries())
        if (!e.is_dir) total += e.size;

    std::ostringstream files;
    uint64_t           done = 0;
    uint8_t            sec[kSector];
    for (const auto& e : iso.entries()) {
        if (e.is_dir) continue;
        checkCancel(cancel);
        if (!safeIsoPath(e.path)) throw RuntimeDataError("caminho invalido no disco: " + e.path);
        const fs::path dst = dir / "disc" / u8path(e.path);
        fs::create_directories(dst.parent_path());
        util::Sha256  sha;
        std::ofstream f(dst, std::ios::binary | std::ios::trunc);
        iso.read(e, [&](const uint8_t* p, size_t n) {
            f.write(reinterpret_cast<const char*>(p), std::streamsize(n));
            sha.update(p, n);
        });
        f.close();
        if (!f) throw RuntimeDataError("falha ao gravar " + pathText(dst));
        const uint32_t n = sectorsOf(e.size);
        for (uint32_t i = 0; i < n && e.lba + i < maxLba; ++i) covered[e.lba + i] = std::max<uint8_t>(covered[e.lba + i], 1);
        if (e.size % kSector && e.lba + n - 1 < maxLba) {  // sobra do ultimo setor
            img.readUserData(e.lba + n - 1, sec);
            const uint32_t tail = e.size % kSector;
            if (std::any_of(sec + tail, sec + kSector, [](uint8_t b) { return b != 0; })) covered[e.lba + n - 1] = 2;
        }
        files << "file " << e.lba << " " << e.size << " " << sha.hexdigest() << " " << e.path << "\n";
        ++res.files;
        res.bytes += e.size;
        done += e.size;
        say(total ? 0.7 * double(done) / double(total) : 0.7, "arquivos do disco: " + std::to_string(res.files));
    }

    // setores que nao vem de arquivo (ou cuja sobra nao e nula): so os nao nulos
    {
        std::ofstream idx(dir / kMetaIdx, std::ios::binary | std::ios::trunc);
        std::ofstream bin(dir / kMetaBin, std::ios::binary | std::ios::trunc);
        uint64_t      dataSectors = 0, scanned = 0;
        for (const auto& t : img.tracks())
            if (t.isData()) dataSectors += t.length;
        for (const auto& t : img.tracks()) {
            if (!t.isData()) continue;
            for (uint32_t lba = t.start_lba; lba < t.start_lba + t.length; ++lba, ++scanned) {
                if ((scanned & 0x3FFF) == 0) {
                    checkCancel(cancel);
                    say(0.7 + 0.3 * double(scanned) / double(dataSectors), "setores do sistema e diretorios");
                }
                if (covered[lba] == 1) continue;
                img.readUserData(lba, sec);
                if (covered[lba] != 2 && std::all_of(sec, sec + kSector, [](uint8_t b) { return b == 0; })) continue;
                idx.write(reinterpret_cast<const char*>(&lba), 4);
                bin.write(reinterpret_cast<const char*>(sec), kSector);
                ++res.metaSectors;
            }
        }
        idx.close();
        bin.close();
        if (!idx || !bin) throw RuntimeDataError("falha ao gravar os setores do sistema em " + pathText(dir));
    }

    // manifesto por ultimo (presenca = dados completos), gravado por troca
    {
        const fs::path tmp = dir / (std::string(kManifest) + ".tmp");
        std::ofstream  m(tmp, std::ios::binary | std::ios::trunc);
        m << "bs-runtime-data " << kRuntimeDataFormat << "\n"
          << "# Dados do Blue Stinger preparados pelo Builder a partir do CDI do usuario. Nao publique.\n"
          << "version " << res.versionId << "\n"
          << "boot " << ip.boot_file << " " << bootSha << " " << (scrambled ? 1 : 0) << " " << boot.start_lba << "\n"
          << "meta " << fileSha256(dir / kMetaIdx) << " " << fileSha256(dir / kMetaBin) << "\n";
        for (const auto& t : img.tracks())
            m << "track " << t.number << " " << t.session << " " << t.start_lba << " " << t.length << " "
              << (t.isData() ? 1 : 0) << "\n";
        m << files.str();
        m.close();
        if (!m) throw RuntimeDataError("falha ao gravar " + std::string(kManifest));
        fs::rename(tmp, dir / kManifest);
    }
    say(1.0, "dados prontos");
    return res;
}

std::string verify_runtime_data(const DiscImage& img, const fs::path& dir, const ProgressFn& progress,
                                const std::atomic<bool>* cancel) {
    RuntimeDisc rd(dir);
    if (rd.manifest().tracks.size() != img.tracks().size()) return "numero de faixas diferente";
    uint64_t dataSectors = 0, scanned = 0;
    for (const auto& t : img.tracks())
        if (t.isData()) dataSectors += t.length;
    uint8_t a[kSector], b[kSector];
    for (const auto& t : img.tracks()) {
        if (!t.isData()) continue;
        for (uint32_t lba = t.start_lba; lba < t.start_lba + t.length; ++lba, ++scanned) {
            if ((scanned & 0x3FFF) == 0) {
                checkCancel(cancel);
                if (progress) progress(double(scanned) / double(dataSectors), "conferindo setores");
            }
            img.readUserData(lba, a);
            if (!rd.readSector(lba, b)) return "setor " + std::to_string(lba) + " ausente nos dados";
            if (std::memcmp(a, b, kSector) != 0) return "setor " + std::to_string(lba) + " diferente da imagem";
        }
    }
    return {};
}

std::string check_runtime_files(const fs::path& dir, const ProgressFn& progress) {
    const RuntimeManifest m = load_runtime_manifest(dir);
    // setores de sistema primeiro (hard links: uma alteracao do mesmo tamanho tambem conta)
    if (fileSha256(dir / kMetaIdx) != m.metaIdxSha256) return std::string("arquivo alterado ou corrompido: ") + kMetaIdx;
    if (fileSha256(dir / kMetaBin) != m.metaBinSha256) return std::string("arquivo alterado ou corrompido: ") + kMetaBin;
    uint64_t              total = 0, done = 0;
    for (const auto& f : m.files) total += f.size;
    std::vector<char> buf(1 << 20);
    for (const auto& f : m.files) {
        std::ifstream in(dir / "disc" / u8path(f.path), std::ios::binary);
        util::Sha256  sha;
        while (in) {
            in.read(buf.data(), std::streamsize(buf.size()));
            sha.update(buf.data(), size_t(in.gcount()));
        }
        if (sha.hexdigest() != f.sha256) return "arquivo alterado ou corrompido: " + f.path;
        done += f.size;
        if (progress) progress(total ? double(done) / double(total) : 1.0, f.path);
    }
    return {};
}

} // namespace bs::disc
