#pragma once
// Arquivos do jogo (N1). Substitui o GD-ROM: o jogo pede arquivos por nome logico (o
// caminho ISO 9660, p.ex. "\BSDATA\CNF00.NT") e le bytes; nao ha setores, FAD nem
// leitura da imagem de disco. A implementacao nativa le de data\ (manifesto + hash) com os
// mods de arquivo por cima.
#include "core/handle.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::files {

struct FileTag;
using FileHandle = Handle<FileTag>;
struct RequestTag;
using RequestId = Handle<RequestTag>;

enum class RequestState : uint8_t { Pending, Done, Failed };

struct RequestStatus {
    RequestState state       = RequestState::Pending;
    size_t       transferred = 0;  // bytes ja copiados para o destino
    Error        error       = Error::None;
};

struct DirEntry {
    std::string name;  // nome logico (sem versao ";1")
    uint64_t    size = 0;
    bool        directory = false;
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    // Caminho logico, separador '\' ou '/', sem diferenciar maiusculas; relativo a raiz do disco.
    virtual Result<FileHandle> open(std::string_view path) = 0;
    virtual Result<uint64_t>   size(FileHandle f) const = 0;
    // Le ate out.size() bytes a partir de offset; devolve quantos leu (0 no fim).
    virtual Result<size_t> read(FileHandle f, uint64_t offset, std::span<std::byte> out) = 0;
    virtual void           close(FileHandle f) = 0;

    // Leitura assincrona: o jogo consulta o estado a cada quadro (como fazia com o GD-ROM).
    // `out` precisa continuar valido ate o estado sair de Pending.
    virtual Result<RequestId> read_async(FileHandle f, uint64_t offset, std::span<std::byte> out) = 0;
    virtual RequestStatus     poll(RequestId r) = 0;
    virtual void              cancel(RequestId r) = 0;

    virtual Result<std::vector<DirEntry>> list(std::string_view directory) const = 0;
};

} // namespace bs::native::files
