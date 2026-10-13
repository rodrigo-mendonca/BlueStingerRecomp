#include "disc/disc_image.h"

namespace bs::disc {

uint32_t Track::userDataOffset() const {
    switch (sector_size) {
    case 2048: return 0;
    case 2336: return 8;                                   // subheader Mode 2
    case 2352: return mode == TrackMode::Mode1 ? 16 : 24;  // sync+header (+subheader)
    default:   throw DiscError("tamanho de setor invalido: " + std::to_string(sector_size));
    }
}

const Track* DiscImage::findTrack(uint32_t lba) const {
    for (const auto& t : tracks_)
        if (t.contains(lba)) return &t;
    return nullptr;
}

const Track& DiscImage::bootTrack() const {
    const Track* best = nullptr;
    for (const auto& t : tracks_)
        if (t.isData() && (!best || t.session >= best->session)) best = &t;
    if (!best) throw DiscError("a imagem nao tem faixa de dados");
    return *best;
}

void DiscImage::readUserData(uint32_t lba, uint8_t* out) const {
    const Track* t = findTrack(lba);
    if (!t) throw DiscError("LBA " + std::to_string(lba) + " fora de todas as faixas");
    if (!t->isData()) throw DiscError("LBA " + std::to_string(lba) + " pertence a uma faixa de audio");
    uint64_t off = t->file_offset + uint64_t(lba - t->start_lba) * t->sector_size + t->userDataOffset();
    readRaw(off, out, kUserDataSize);
}

InputFile::InputFile(const std::filesystem::path& path) {
#ifdef _WIN32
    f_ = _wfopen(path.c_str(), L"rb");
#else
    f_ = std::fopen(path.c_str(), "rb");
#endif
    if (!f_) throw DiscError("nao foi possivel abrir " + path.string());
    std::error_code ec;
    size_ = std::filesystem::file_size(path, ec);
    if (ec) throw DiscError("nao foi possivel obter o tamanho de " + path.string());
}

InputFile::~InputFile() {
    if (f_) std::fclose(f_);
}

void InputFile::read(uint64_t offset, void* dst, size_t n) const {
    if (offset > size_ || n > size_ - offset)
        throw DiscError("leitura alem do fim do arquivo (offset " + std::to_string(offset) + ")");
#ifdef _WIN32
    int rc = _fseeki64(f_, static_cast<long long>(offset), SEEK_SET);
#else
    int rc = fseeko(f_, static_cast<off_t>(offset), SEEK_SET);
#endif
    if (rc != 0 || std::fread(dst, 1, n, f_) != n)
        throw DiscError("falha de leitura no offset " + std::to_string(offset));
}

} // namespace bs::disc
