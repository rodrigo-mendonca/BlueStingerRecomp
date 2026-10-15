#pragma once
// Saves (N4). Substitui o VMU e a biblioteca de backup: arquivos de save logicos, guardados
// em disco no formato nativo, com o conteudo do jogo preservado e versao para migracao.
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::saves {

struct SaveInfo {
    std::string name;          // nome logico do arquivo de save (p.ex. "BLUESTIN.000")
    std::string comment;       // descricao exibida pelo jogo
    uint32_t    blocks = 0;    // tamanho em blocos de 512 bytes (o jogo mostra e confere)
    int64_t     modified_unix = 0;
    uint32_t    format_version = 0;  // versao do formato nativo do arquivo
};

class ISaveStore {
public:
    virtual ~ISaveStore() = default;
    // `unit` identifica o cartao logico (o jogo escolhe; 0 = primeiro).
    virtual bool                          present(int unit) const = 0;
    virtual uint32_t                      free_blocks(int unit) const = 0;
    virtual Result<std::vector<SaveInfo>> list(int unit) const = 0;
    virtual Result<std::vector<std::byte>> load(int unit, std::string_view name) = 0;
    virtual Result<void> save(int unit, const SaveInfo& info, std::span<const std::byte> data) = 0;
    virtual Result<void> remove(int unit, std::string_view name) = 0;
};

} // namespace bs::native::saves
