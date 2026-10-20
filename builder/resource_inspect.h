#pragma once
// Inspecao dos recursos de uma instalacao ja gerada (BlueStinger-Developer.exe), sem o CDI: so o que tem
// metadados confiaveis. Filmes pelo mesmo catalogo que o jogo usa (data\video\manifest.toml + mods\video,
// native/video/movie_catalog), entao a origem e o estado mostrados sao os que o jogo vai ver. Audio so
// quando houver o manifesto do N6 (data\audio\manifest.toml); ate la, aparece como indisponivel, nunca como
// lista inventada. Devolve codigos (a janela traduz pelo catalogo de textos), nada de texto de interface.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bs::builder {

enum class ResourceKind : uint8_t { Movie, Audio, Config, Functions, Generated, Mod, Project };
enum class ResourceState : uint8_t {
    Ok,           // o arquivo existe
    FileMissing,  // o manifesto aponta um arquivo que nao esta la
    Absent,       // nao existe no disco original e nenhum mod o fornece (o jogo pula)
    NotInstalled, // a familia toda nao foi instalada (p.ex. sem a pasta data\video: instalacao sem filmes)
    Pending,      // a familia ainda nao tem manifesto (audio ate o N6)
    Unverified,   // marca do formato atual presente; o conteudo (hash de cada arquivo) so e conferido ao compilar
};
enum class ResourceOrigin : uint8_t { Builder, Mod, Installation, None };

struct ResourceRow {
    ResourceKind          kind = ResourceKind::Movie;
    std::string           id;        // ID logico estavel ("3"), vazio quando a familia toda
    std::string           name;      // nome original ("E001.SFD") ou do arquivo de configuracao
    ResourceState         state = ResourceState::Ok;
    ResourceOrigin        origin = ResourceOrigin::None;
    std::filesystem::path file;      // o que o jogo abre (vazio se nada)
    std::filesystem::path manifest;  // de onde veio a entrada
    bool                  moddable = false;  // substituivel por mod sem recompilar
    std::string           detail;    // tecnico, igual em qualquer idioma (resolucao, quadros, legendas)
};

enum class InspectProblem : uint8_t { NoInstallation, NoExecutable, NoData, ManifestError };
struct InspectIssue {
    InspectProblem kind;
    std::string    detail;  // caminho ou mensagem do leitor do manifesto
};

struct ResourceReport {
    bool                      project = false;  // a pasta e um projeto exportado (nao uma instalacao)
    std::filesystem::path     installDir;  // a pasta com BlueStinger.exe (aceita tambem a pasta acima dela)
    std::vector<ResourceRow>  rows;
    std::vector<InspectIssue> issues;
};

// Uma instalacao (BlueStinger.exe + data) ou um projeto exportado (mapa_funcoes.tsv, generated/): no projeto,
// o mapa de funcoes (contagem por origem), o C++ gerado, os mods (C++, assets, video), o CMakePresets.json, a
// pasta dos recursos do bs-projeto.toml (pronta = compilar sem o CDI) e os filmes como o jogo os resolveria com
// os mods do projeto.
ResourceReport inspect_resources(const std::filesystem::path& dir, const std::wstring& installName = L"BlueStinger");

} // namespace bs::builder
