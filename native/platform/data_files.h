#pragma once
// N1: sistema de arquivos nativo do jogo sobre arquivos comuns do Windows.
//
// Raiz: a arvore de recursos preparada pelo Builder (data\disc\...). O indice e montado
// percorrendo a pasta: nome logico (sem diferenciar maiusculas, '\' ou '/', sem o sufixo de
// versao ";1") -> arquivo. Nada de setores, enderecos de disco, sistema de arquivos do console
// ou manifesto do disco: so caminhos e bytes.
//
// Mods de recursos (pasta opcional, mesma arvore): cada arquivo substitui o recurso de mesmo
// nome logico, de qualquer tamanho. Um mod sem recurso correspondente, que nao seja arquivo
// regular ou que nao possa ser lido e erro (DataFilesError com o caminho e o motivo).
#include "platform/files.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace bs::native::files {

struct DataFilesError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct AppliedMod {
    std::string logical;  // nome logico ("BSDATA/CNF00.NT")
    uint64_t    originalSize = 0, modSize = 0;
};

class DataFileSystem final : public IFileSystem {
public:
    DataFileSystem(const std::filesystem::path& root, const std::filesystem::path& mods = {});
    ~DataFileSystem() override;
    DataFileSystem(const DataFileSystem&)            = delete;
    DataFileSystem& operator=(const DataFileSystem&) = delete;

    Result<FileHandle> open(std::string_view path) override;
    Result<uint64_t>   size(FileHandle f) const override;
    Result<size_t>     read(FileHandle f, uint64_t offset, std::span<std::byte> out) override;
    void               close(FileHandle f) override;
    Result<RequestId>  read_async(FileHandle f, uint64_t offset, std::span<std::byte> out) override;
    RequestStatus      poll(RequestId r) override;
    void               cancel(RequestId r) override;
    Result<std::vector<DirEntry>> list(std::string_view directory) const override;

    // Nome logico canonico (maiusculas, '/', sem ";1" nem barra inicial).
    static std::string normalize(std::string_view path);

    const std::vector<AppliedMod>& mods() const { return mods_; }
    size_t                         file_count() const { return index_.size(); }
    std::string                    logical_name(FileHandle f) const;  // "" se invalido

private:
    struct Entry {
        std::filesystem::path path;
        uint64_t              size = 0;
        std::string           logical;
    };
    struct Open {
        const Entry* entry = nullptr;
        std::FILE*   f     = nullptr;
    };

    std::map<std::string, Entry>  index_;  // por nome logico canonico
    std::map<uint32_t, Open>      open_;
    std::map<uint32_t, RequestStatus> requests_;
    uint32_t                      nextHandle_ = 1, nextRequest_ = 1;
    std::vector<AppliedMod>       mods_;
};

} // namespace bs::native::files
