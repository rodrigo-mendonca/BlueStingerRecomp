#pragma once
// TextLocalization (docs/LOCALIZACAO-PLANO.md, secao 4): troca textos que JA existem no jogo por traducoes de
// mods, por ID estavel; sem traducao, o jogo usa o original (nada muda). Arquivo: <pacote>\textos\<idioma>.toml
//   [textos]
//   "menu.novo_jogo" = "NOVO JOGO"
// Os codigos especiais do motor de texto do jogo (cores, quebras, variaveis) tem de sobreviver a traducao: quem
// integra fornece o extrator (Tokenizer) desses codigos e a traducao que muda o conjunto e recusada (fica o
// original, com o problema registrado). Os codigos do Blue Stinger ainda nao foram mapeados (PENDENTE, N9):
// sem extrator, so a validacao de ID e de UTF-8 vale. Portavel.
#include "localization/mod_packages.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::localization {

using Tokenizer = std::function<std::vector<std::string>(std::string_view text)>;

class TextLocalization {
public:
    // Junta os textos dos pacotes para `language`: o idioma exato vence o recuo; no mesmo idioma, o pacote de
    // maior prioridade vence. IDs invalidos, texto nao UTF-8 e arquivos ilegiveis vao para problems().
    static TextLocalization load(const ModPackages& mods, std::string_view language);
    // A traducao do ID, ja conferida contra o original (codigos especiais iguais); nullopt = use o original
    std::optional<std::string> translate(std::string_view id, std::string_view original, const Tokenizer& codes = {}) const;
    size_t                          size() const { return texts_.size(); }
    const std::vector<std::string>& problems() const { return problems_; }

private:
    struct Entry {
        std::string text, package, language;
    };
    std::map<std::string, Entry, std::less<>> texts_;
    mutable std::vector<std::string>          problems_;
};

} // namespace bs::native::localization
