#pragma once
// Leitor de imagens DiscJuggler (.cdi) v2, v3 e v3.5.
#include "disc/disc_image.h"

#include <memory>

namespace bs::disc {

class CdiImage final : public DiscImage {
public:
    explicit CdiImage(const std::filesystem::path& path);

    std::string formatName() const override;
    uint32_t    version() const { return version_; }

    // Exposto para testes: interpreta o cabeçalho ja lido.
    // `header` = bytes do inicio do cabeçalho ate o trailer (exclusive).
    static std::vector<Track> parseHeader(const std::vector<uint8_t>& header, uint32_t version,
                                          uint64_t data_end);

    static constexpr uint32_t kV2  = 0x80000004;
    static constexpr uint32_t kV3  = 0x80000005;
    static constexpr uint32_t kV35 = 0x80000006;

protected:
    void readRaw(uint64_t offset, void* dst, size_t n) const override { file_->read(offset, dst, n); }

private:
    std::unique_ptr<InputFile> file_;
    uint32_t                   version_ = 0;
};

} // namespace bs::disc
