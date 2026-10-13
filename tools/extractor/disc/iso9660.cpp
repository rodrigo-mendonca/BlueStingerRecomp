#include "disc/iso9660.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace bs::disc {
namespace {

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

std::string trimRight(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

// Nomes vem do disco (dado nao confiavel): so caracteres de nome ISO9660 e sem componentes especiais.
std::string sanitizeName(const uint8_t* p, size_t n) {
    std::string s(reinterpret_cast<const char*>(p), n);
    if (auto semi = s.find(';'); semi != std::string::npos) s.resize(semi);
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty() || s == "." || s == "..") throw DiscError("nome de arquivo ISO9660 invalido");
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-'))
            throw DiscError("caractere invalido em nome ISO9660: " + s);
    return s;
}

bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}

} // namespace

Iso9660::Iso9660(const DiscImage& disc, uint32_t pvd_lba) : disc_(disc) {
    uint8_t pvd[kUserDataSize];
    disc_.readUserData(pvd_lba, pvd);
    if (pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0)
        throw DiscError("Primary Volume Descriptor ISO9660 nao encontrado na LBA " + std::to_string(pvd_lba));
    volume_id_         = trimRight(std::string(reinterpret_cast<char*>(pvd + 40), 32));
    const uint8_t* root = pvd + 156;
    walk(le32(root + 2), le32(root + 10), "", 0);
}

void Iso9660::walk(uint32_t lba, uint32_t size, const std::string& prefix, int depth) {
    if (depth > 16) throw DiscError("diretorios ISO9660 aninhados demais");
    const uint32_t sectors = (size + kUserDataSize - 1) / kUserDataSize;
    uint8_t        buf[kUserDataSize];
    for (uint32_t s = 0; s < sectors; ++s) {
        disc_.readUserData(lba + s, buf);
        uint32_t o = 0;
        while (o < kUserDataSize) {
            const uint8_t len = buf[o];
            if (len == 0) break;  // registros nao cruzam setores: resto do setor e padding
            if (len < 34 || o + len > kUserDataSize) throw DiscError("registro de diretorio ISO9660 corrompido");
            const uint8_t* r       = buf + o;
            const uint8_t  namelen = r[32];
            if (33u + namelen > len) throw DiscError("nome ISO9660 ultrapassa o registro");
            const bool special = namelen == 1 && (r[33] == 0 || r[33] == 1);
            if (!special) {
                IsoEntry e;
                e.path   = prefix + sanitizeName(r + 33, namelen);
                e.lba    = le32(r + 2);
                e.size   = le32(r + 10);
                e.is_dir = (r[25] & 2) != 0;
                entries_.push_back(e);
                if (e.is_dir) walk(e.lba, e.size, e.path + "/", depth + 1);
            }
            o += len;
        }
    }
}

const IsoEntry* Iso9660::find(const std::string& path) const {
    for (const auto& e : entries_)
        if (iequals(e.path, path)) return &e;
    return nullptr;
}

void Iso9660::read(const IsoEntry& e, const std::function<void(const uint8_t*, size_t)>& sink) const {
    uint8_t  buf[kUserDataSize];
    uint32_t left = e.size;
    for (uint32_t lba = e.lba; left > 0; ++lba) {
        disc_.readUserData(lba, buf);
        const uint32_t n = std::min(left, kUserDataSize);
        sink(buf, n);
        left -= n;
    }
}

std::vector<uint8_t> Iso9660::readAll(const IsoEntry& e) const {
    std::vector<uint8_t> out;
    out.reserve(e.size);
    read(e, [&](const uint8_t* p, size_t n) { out.insert(out.end(), p, p + n); });
    return out;
}

} // namespace bs::disc
