#pragma once
// N5 (docs/N5-PLANO.md, secao 4.2): catalogo dos filmes por ID logico. O Builder grava
// data\video\manifest.toml com os filmes convertidos do disco do usuario (a referencia de
// fidelidade); mods em mods\video substituem por ID sem recompilar: um arquivo com o nome do
// original (p.ex. NEWGAME.mp4) ou uma entrada em mods\video\manifest.toml. Legendas (.srt/.vtt) por
// idioma seguem a mesma regra. Sem nada do Dreamcast: o ID e o nome original sao so chaves.
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::video {

struct MovieEntry {
    uint32_t              id = 0;
    std::string           original;   // nome no disco original, maiusculo (p.ex. "NEWGAME.SFD")
    std::filesystem::path file;       // arquivo a tocar (absoluto)
    bool                  mod = false;  // veio de mods\video (modo modificado: duracao do proprio arquivo)
    // metadados da conversao (modo fiel); zero quando o mod nao informa
    uint32_t    frames = 0, width = 0, height = 0;
    double      fps = 0;
    std::string sha256;  // do arquivo do Builder (diagnostico de recurso trocado ou corrompido)
    bool        absent = false;  // o disco original nao tem o filme (e nenhum mod o fornece)
    int32_t     audioDelaySamples = 0;  // atraso do audio no arquivo (priming do AAC), por canal
    int64_t     videoEnd100ns = 0, audioEnd100ns = 0;  // fins de cada stream na fonte original
    std::map<std::string, std::filesystem::path> subtitles;  // idioma -> .srt/.vtt
};

struct CatalogOptions {
    std::filesystem::path dataDir;  // data\video (manifesto do Builder)
    std::filesystem::path modsDir;  // mods\video (opcional)
};

class MovieCatalog {
public:
    // Le os manifestos. Erros de sintaxe ou campos invalidos nao abortam: vao para `problems` e a
    // entrada e ignorada (o filme cai no diagnostico de recurso ausente).
    static MovieCatalog load(const CatalogOptions& o);

    const MovieEntry*               find(uint32_t id) const;
    const MovieEntry*               find(std::string_view original) const;  // sem diferenciar caixa
    const std::vector<std::string>& problems() const { return problems_; }
    size_t                          size() const { return byId_.size(); }
    // todas as entradas por ID (inspecao de recursos do Developer; so leitura)
    const std::map<uint32_t, MovieEntry>& all() const { return byId_; }

private:
    std::map<uint32_t, MovieEntry> byId_;
    std::vector<std::string>       problems_;
};

} // namespace bs::native::video
