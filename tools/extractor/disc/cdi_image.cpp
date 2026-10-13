#include "disc/cdi_image.h"

#include <cstring>

namespace bs::disc {
namespace {

// Marca que abre cada descritor de faixa no cabecalho.
constexpr uint8_t kTrackMark[20] = {0, 0, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0, 0, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& b) : b_(b) {}

    void skip(size_t n) {
        need(n);
        pos_ += n;
    }
    uint32_t u8() { return take(1); }
    uint32_t u16() { return take(2); }
    uint32_t u32() { return take(4); }
    bool     matches(const uint8_t* p, size_t n) const { return pos_ + n <= b_.size() && std::memcmp(&b_[pos_], p, n) == 0; }
    size_t   pos() const { return pos_; }

private:
    uint32_t take(size_t n) {
        need(n);
        uint32_t v = 0;
        for (size_t i = 0; i < n; ++i) v |= uint32_t(b_[pos_ + i]) << (8 * i);
        pos_ += n;
        return v;
    }
    void need(size_t n) const {
        if (pos_ + n > b_.size()) throw DiscError("cabecalho CDI truncado (offset " + std::to_string(pos_) + ")");
    }
    const std::vector<uint8_t>& b_;
    size_t                      pos_ = 0;
};

uint32_t sectorSizeFromCode(uint32_t code) {
    switch (code) {
    case 0: return 2048;
    case 1: return 2336;
    case 2: return 2352;
    default: throw DiscError("codigo de tamanho de setor CDI desconhecido: " + std::to_string(code));
    }
}

} // namespace

std::vector<Track> CdiImage::parseHeader(const std::vector<uint8_t>& header, uint32_t version, uint64_t data_end) {
    Reader             r(header);
    std::vector<Track> tracks;
    uint64_t           pos      = 0;  // os dados das faixas ficam em sequencia desde o offset 0
    const uint32_t     sessions = r.u16();
    if (sessions == 0 || sessions > 99) throw DiscError("numero de sessoes CDI invalido: " + std::to_string(sessions));

    for (uint32_t s = 1; s <= sessions; ++s) {
        const uint32_t count = r.u16();
        if (count > 99) throw DiscError("numero de faixas CDI invalido: " + std::to_string(count));
        for (uint32_t i = 0; i < count; ++i) {
            if (r.u32() != 0) r.skip(8);  // dados extras de versoes 3.00.780+
            if (!r.matches(kTrackMark, sizeof(kTrackMark)))
                throw DiscError("marca de faixa CDI ausente no offset " + std::to_string(r.pos()));
            r.skip(sizeof(kTrackMark));
            r.skip(4);
            r.skip(r.u8());  // nome do arquivo original
            r.skip(11 + 4 + 4);
            if (r.u32() == 0x80000000) r.skip(8);  // DiscJuggler 4
            r.skip(2);
            Track t;
            t.session = static_cast<int>(s);
            t.number  = static_cast<int>(tracks.size() + 1);
            t.pregap  = r.u32();
            t.length  = r.u32();
            r.skip(6);
            const uint32_t mode = r.u32();
            if (mode > 2) throw DiscError("modo de faixa CDI desconhecido: " + std::to_string(mode));
            t.mode = static_cast<TrackMode>(mode);
            r.skip(12);
            t.start_lba          = r.u32();
            const uint32_t total = r.u32();
            r.skip(16);
            t.sector_size = sectorSizeFromCode(r.u32());
            r.skip(29);
            if (version != kV2) {
                r.skip(5);
                if (r.u32() == 0xFFFFFFFF) r.skip(78);
            }
            if (total < uint64_t(t.pregap) + t.length) throw DiscError("faixa CDI com comprimento inconsistente");
            t.file_offset = pos + uint64_t(t.pregap) * t.sector_size;
            pos += uint64_t(total) * t.sector_size;
            if (pos > data_end) throw DiscError("faixa CDI ultrapassa o fim dos dados");
            tracks.push_back(t);
        }
        r.skip(4 + 8);  // fim de sessao
        if (version != kV2) r.skip(1);
    }
    if (tracks.empty()) throw DiscError("imagem CDI sem faixas");
    return tracks;
}

CdiImage::CdiImage(const std::filesystem::path& path) : file_(std::make_unique<InputFile>(path)) {
    const uint64_t size = file_->size();
    if (size < 8) throw DiscError("arquivo pequeno demais para ser CDI");
    uint32_t trailer[2];
    file_->read(size - 8, trailer, 8);
    version_          = trailer[0];
    const uint32_t ho = trailer[1];
    if (version_ != kV2 && version_ != kV3 && version_ != kV35)
        throw DiscError("nao e uma imagem CDI reconhecida (versao 0x" + [&] {
            char b[9];
            std::snprintf(b, sizeof(b), "%08X", version_);
            return std::string(b);
        }() + ")");
    const uint64_t header_pos = version_ == kV35 ? size - ho : ho;
    if (ho == 0 || header_pos >= size - 8) throw DiscError("offset de cabecalho CDI invalido");
    std::vector<uint8_t> header(static_cast<size_t>(size - 8 - header_pos));
    file_->read(header_pos, header.data(), header.size());
    tracks_ = parseHeader(header, version_, header_pos);
}

std::string CdiImage::formatName() const {
    switch (version_) {
    case kV2: return "CDI v2";
    case kV3: return "CDI v3";
    default: return "CDI v3.5";
    }
}

} // namespace bs::disc
