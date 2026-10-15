#pragma once
// Dados de execucao: o disco do usuario convertido em arquivos, para o jogo nativo rodar
// sem o CDI. O Builder prepara (prepare_runtime_data) a partir da imagem; o runtime le
// (RuntimeDisc) e reconstroi qualquer setor de dados que o jogo pedir ao GD-ROM.
//
//   <dir>/disc.manifest   texto UTF-8: versao, faixas, arquivos (LBA, tamanho, SHA-256)
//   <dir>/disc/<caminho>  cada arquivo do ISO9660, com os bytes exatamente como no disco
//   <dir>/disc_meta.idx   LBAs (uint32 LE, crescentes) dos setores que nao vem de arquivo:
//   <dir>/disc_meta.bin   area do sistema (IP.BIN), descritores, diretorios, sobras
//                         nao nulas do ultimo setor de um arquivo; 2048 bytes cada
//
// Setor de dados = setor de meta se houver; senao, o trecho do arquivo que o cobre
// (completado com zeros); senao, zeros. A preparacao confere todos os setores das faixas
// de dados contra a imagem (verify_runtime_data).
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace bs::disc {

class DiscImage;

// 2: o manifesto inclui o SHA-256 de disc_meta.idx/.bin (revisao Codex 2026-10-15)
constexpr int kRuntimeDataFormat = 2;

struct RuntimeDataError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct RuntimeTrack {
    uint32_t number = 0, session = 0, start_lba = 0, length = 0;
    bool     data = true;
};

struct RuntimeFile {
    uint32_t    lba = 0, size = 0;
    std::string path;    // relativo a disc/, separador '/', UTF-8
    std::string sha256;  // dos bytes do arquivo
};

struct RuntimeManifest {
    int                       format = 0;
    std::string               versionId;    // known_versions.h
    std::string               bootFile;     // "1ST_READ.BIN" (IP.BIN)
    std::string               bootSha256;   // executavel desembaralhado (hash da versao)
    bool                      bootScrambled = false;
    uint32_t                  bootTrackLba = 0;  // inicio da faixa de boot (IP.BIN)
    std::string               metaIdxSha256, metaBinSha256;  // setores de sistema (conferidos no --verify-data)
    std::vector<RuntimeTrack> tracks;
    std::vector<RuntimeFile>  files;
};

// Le e confere a estrutura (versao do formato, arquivos presentes com o tamanho certo).
// Lanca RuntimeDataError com mensagem para o usuario (o que falta, o que fazer).
RuntimeManifest load_runtime_manifest(const std::filesystem::path& dir);

// Leitor de setores a partir dos dados preparados.
class RuntimeDisc {
public:
    // overrides: pasta de mods de arquivos (mods/assets): <overrides>/<caminho do ISO>
    // substitui o arquivo do disco. Precisa caber no espaco original (mesmo tamanho ou
    // menor, completado com zeros); maior lanca RuntimeDataError com a explicacao.
    explicit RuntimeDisc(const std::filesystem::path& dir, const std::filesystem::path& overrides = {});
    ~RuntimeDisc();
    RuntimeDisc(const RuntimeDisc&)            = delete;
    RuntimeDisc& operator=(const RuntimeDisc&) = delete;

    const RuntimeManifest&          manifest() const { return m_; }
    const std::vector<std::string>& overridden() const { return overriddenNames_; }  // mods aplicados
    // Setor de dados (2048 bytes). false fora das faixas de dados. Lanca RuntimeDataError
    // se um arquivo sumir ou nao puder ser lido durante a execucao.
    bool readSector(uint32_t lba, uint8_t* out);
    // Arquivo inteiro (caminho do ISO, sem diferenciar maiusculas).
    std::vector<uint8_t> readFile(const std::string& isoPath);
    // IP.BIN (16 setores do inicio da faixa de boot).
    std::vector<uint8_t> ipBin();

private:
    FILE* open(size_t fileIndex);

    std::filesystem::path   dir_;
    RuntimeManifest         m_;
    std::vector<uint32_t>   metaLba_;
    FILE*                   meta_ = nullptr;
    std::vector<size_t>     byLba_;  // indices de m_.files em ordem de LBA
    std::filesystem::path   overrides_;
    std::vector<uint32_t>   overrideSize_;  // por arquivo: 0 = original; senao, tamanho do mod + 1
    std::vector<std::string> overriddenNames_;
    struct Open {
        size_t index;
        FILE*  f;
    };
    std::vector<Open> open_;  // poucos arquivos abertos (o mais recente no fim)
};

struct PrepareResult {
    size_t   files = 0;
    uint64_t bytes = 0;
    uint32_t metaSectors = 0;
    std::string versionId;
};

using ProgressFn = std::function<void(double fraction, const std::string& what)>;

// Prepara os dados de execucao em `dir` (criada; deve estar vazia ou nao existir) a partir
// da imagem. Exige a versao suportada. Cancelamento: lanca RuntimeDataError("cancelado").
PrepareResult prepare_runtime_data(const DiscImage& img, const std::filesystem::path& dir,
                                   const ProgressFn& progress = {}, const std::atomic<bool>* cancel = nullptr);

// Compara todos os setores das faixas de dados da imagem com os reconstruidos de `dir`.
// Vazio = iguais; senao, a primeira diferenca.
std::string verify_runtime_data(const DiscImage& img, const std::filesystem::path& dir,
                                const ProgressFn& progress = {}, const std::atomic<bool>* cancel = nullptr);

// Confere o SHA-256 de todos os arquivos (inclusive disc_meta.idx/.bin) contra o manifesto.
// Vazio = ok.
std::string check_runtime_files(const std::filesystem::path& dir, const ProgressFn& progress = {});

} // namespace bs::disc
