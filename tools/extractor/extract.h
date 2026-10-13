#pragma once
#include <filesystem>
#include <iosfwd>

namespace bs {

struct ExtractOptions {
    std::filesystem::path image;
    std::filesystem::path out;
    bool                  force     = false;  // aceita 1ST_READ.BIN com hash desconhecido
    bool                  info_only = false;  // so mostra faixas, IP.BIN e hash; nao grava nada
};

// Retorna 0 em sucesso. Mensagens de progresso vao para `log`; erros lancam excecao.
int runExtract(const ExtractOptions& opt, std::ostream& log);

} // namespace bs
