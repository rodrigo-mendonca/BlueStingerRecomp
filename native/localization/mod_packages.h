#pragma once
// Pacotes de mods de localizacao (docs/LOCALIZACAO-PLANO.md, secao 7): cada pasta mods\<pacote>\ com um
// mod.toml. O jogo descobre, valida e ordena os pacotes; os componentes (textos, legendas, vozes) pedem um
// recurso relativo e recebem o arquivo do pacote de maior prioridade que o tem, no idioma pedido ou no recuo
// (pt-BR -> pt). Sem pacotes, nada muda: o jogo segue o original. Portavel, sem o CDI.
//
// mod.toml:
//   formato    = 1                    # versao deste contrato (o jogo recusa formato maior que o suportado)
//   id         = "traducao-ptbr"      # ID estavel (locale.h: valid_resource_id), unico entre os pacotes
//   nome       = "Tradução PT-BR"     # para mostrar
//   versao     = "1.2.0"              # do pacote (livre)
//   prioridade = 100                  # maior vence; empate: ordem do id (determinístico)
//   idiomas    = ["pt-BR"]            # BCP-47 que o pacote fornece (locale.h: valid_language)
//   jogo       = "T13001N V1.000"     # opcional: so carrega para esta versao do jogo
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::localization {

inline constexpr int kModFormat = 1;

struct ModPackage {
    std::string           id, name, version;
    int                   priority = 0;
    bool                  provisional = false;  // provisorio = true: conteudo nao revisado (ex.: legendas por IA)
    std::vector<std::string> languages;  // canonicos
    std::filesystem::path dir;           // canonico (o que foi conferido e o que se abre)
};

struct ModProblem {
    std::filesystem::path dir;
    std::string           message;  // por que o pacote foi recusado (o jogo segue sem ele)
};

class ModPackages {
public:
    // Le mods\*\mod.toml. Pacotes invalidos ou incompativeis ficam em problems() e nao entram.
    static ModPackages discover(const std::filesystem::path& modsDir, std::string_view gameVersion = {});
    const std::vector<ModPackage>& packages() const { return packages_; }  // em ordem de prioridade
    const std::vector<ModProblem>& problems() const { return problems_; }
    // Arquivo `relative` (ex.: "legendas/E001.srt") do pacote de maior prioridade que o tem, procurando por
    // idioma em `<relative com {idioma}>`: `pattern` usa "{idioma}" (ex.: "textos/{idioma}.toml"); o recuo e o
    // de language_fallback. Caminho com "..", absoluto ou que sai do pacote (juncao) e recusado (vazio).
    struct Found {
        std::filesystem::path file;
        const ModPackage*     package = nullptr;
        std::string           language;  // o idioma do arquivo achado ("" = sem idioma no padrao)
    };
    std::optional<Found> find(std::string_view pattern, std::string_view language) const;

private:
    std::vector<ModPackage> packages_;
    std::vector<ModProblem> problems_;
};

// Caminho relativo seguro dentro de `root` (sem "..", nao absoluto, e o canonico continua dentro de `root`).
std::optional<std::filesystem::path> safe_join(const std::filesystem::path& root, std::string_view relative);

} // namespace bs::native::localization
