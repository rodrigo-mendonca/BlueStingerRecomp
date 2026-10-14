#pragma once
// Abstracao de imagem de disco: faixas + leitura de setor de dados (2048 bytes)
// por LBA absoluta. CDI hoje; GDI/CHD entram como novas subclasses.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace bs::disc {

constexpr uint32_t kUserDataSize = 2048;

// Caminho como texto UTF-8 para mensagens. path::string() no Windows converte para a
// pagina de codigo ANSI e lanca excecao com caracteres fora dela (nomes com acentos ou
// outros alfabetos): uma mensagem de erro nao pode virar outro erro.
inline std::string pathText(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

struct DiscError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class TrackMode : uint8_t { Audio = 0, Mode1 = 1, Mode2 = 2 };

struct Track {
    int       session     = 0;   // 1-based
    int       number      = 0;   // 1-based, global
    TrackMode mode        = TrackMode::Audio;
    uint32_t  start_lba   = 0;   // LBA do primeiro setor apos o pregap
    uint32_t  length      = 0;   // setores apos o pregap
    uint32_t  pregap      = 0;
    uint32_t  sector_size = 0;   // 2048, 2336 ou 2352
    uint64_t  file_offset = 0;   // offset no arquivo do setor start_lba

    bool     isData() const { return mode != TrackMode::Audio; }
    bool     contains(uint32_t lba) const { return lba >= start_lba && lba - start_lba < length; }
    uint32_t userDataOffset() const;  // offset dos 2048 bytes de dados dentro do setor bruto
};

class DiscImage {
public:
    virtual ~DiscImage() = default;

    const std::vector<Track>& tracks() const { return tracks_; }
    const Track*              findTrack(uint32_t lba) const;
    // Faixa de dados da ultima sessao (onde ficam IP.BIN e o ISO9660 no Dreamcast).
    const Track&              bootTrack() const;

    // Le os 2048 bytes de dados do setor `lba`. Lanca DiscError fora das faixas de dados.
    void readUserData(uint32_t lba, uint8_t* out) const;

    virtual std::string formatName() const = 0;

protected:
    virtual void readRaw(uint64_t offset, void* dst, size_t n) const = 0;
    std::vector<Track> tracks_;
};

// Arquivo binario somente leitura com offsets de 64 bits.
class InputFile {
public:
    explicit InputFile(const std::filesystem::path& path);
    ~InputFile();
    InputFile(const InputFile&)            = delete;
    InputFile& operator=(const InputFile&) = delete;

    uint64_t size() const { return size_; }
    void     read(uint64_t offset, void* dst, size_t n) const;

private:
    std::FILE* f_    = nullptr;
    uint64_t   size_ = 0;
};

} // namespace bs::disc
