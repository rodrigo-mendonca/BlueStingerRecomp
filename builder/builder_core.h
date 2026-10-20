#pragma once
// Builder: do CDI do usuario ao BlueStinger.exe nativo, na maquina do usuario.
//
// Etapas: valida as entradas -> localiza o compilador -> le o CDI e confere o hash do
// executavel -> extrai IP.BIN/1ST_READ.BIN -> analisa e gera o C++ -> configura (CMake) ->
// compila e liga com o runtime (kit de fontes) -> monta o pacote numa pasta temporaria ->
// verifica o executavel montado (--version e boot headless) -> troca a versao instalada.
//
// Saida em <out>:
//   BlueStinger\    jogo instalado (so trocado depois de montado e verificado)
//   .bs-build\      trabalho: game-data, generated, analysis, builder.log
// Build incremental do CMake: %LOCALAPPDATA%\BlueStingerRecomp\build\<hash da saida>
// (caminho curto: o MSVC nao passa de 260 caracteres).
// Uma falha em qualquer etapa deixa a versao instalada anterior intacta.
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

namespace bs::builder {

enum class Step { Validate, Toolchain, Extract, Recompile, Configure, Compile, Package, Verify, Export, Install, Done };
const char* step_name(Step s);

struct Request {
    std::filesystem::path disc;    // imagem CDI do usuario
    std::filesystem::path outDir;  // pasta de saida (criada se nao existir)
    std::wstring          installName = L"BlueStinger";  // subpasta do jogo em outDir
    std::string           versionLabel;  // vazio = arquivo VERSION do kit (ou "dev")
    // Projeto C++ editavel (project_export.h): exportar depois de compilar e verificar;
    // ou compilar A PARTIR de um projeto (com os mods; o CDI so e preciso se os dados ainda
    // nao foram preparados nesta saida).
    std::filesystem::path exportProject;
    std::filesystem::path fromProject;
    std::filesystem::path kitDir;  // fontes do runtime + jogo (MIT/zlib), config e textos
    std::filesystem::path sdl3Dir; // fontes do SDL3; vazio = <kit>\third_party\SDL3
    std::filesystem::path vswhere, vcvars;  // localizacao do compilador (vazio = automatica)
    unsigned              verifyVblanks = 120;  // boot headless na verificacao
    bool                  clean         = false;  // apaga o build incremental antes
    // N5: converte os filmes do disco (SFD) em data\video\*.mp4 + manifest.toml. Sem eles o jogo
    // segue (os filmes ficam ausentes); --sem-filmes poupa a conversao nos testes que nao os usam.
    bool                  movies        = true;
    // Local dos saves gravado em <instalacao>\instalacao.toml (native/platform/install_config.h).
    // Vazio: o arquivo da instalacao anterior fica como esta (arquivo do usuario, preservado).
    std::optional<bool>   portableSaves;
};

struct Callbacks {
    // fracao: progresso total de 0 a 1. Pode ser chamada de outra thread que nao a da GUI.
    std::function<void(Step, double fraction, const std::string& message)> progress;
    std::function<void(const std::string& line)>                           log;
    const std::atomic<bool>*                                               cancel = nullptr;
};

// Tipo do erro, para a interface simples mostrar uma mensagem traduzida (a mensagem tecnica fica em
// Result::error e no builder.log).
enum class ErrorKind { None, Cancelled, MissingInput, DiscNotFound, InvalidDisc, NoSpace, NoPermission, PathTooLong,
                       Busy, Toolchain, Data, Build, Verify, Install, Other };
const char* error_key(ErrorKind k);  // chave do catalogo da interface ("err.no_space"...)
// Falta de espaco e de permissao pelo codigo do sistema; senao `otherwise`.
ErrorKind error_kind_of(const std::error_code& ec, ErrorKind otherwise);

struct Result {
    bool                  ok = false;
    ErrorKind             kind = ErrorKind::None;
    bool                  cancelled = false;
    Step                  failedStep = Step::Validate;
    std::string           error;       // mensagem para o usuario
    std::string           warning;     // sucesso com ressalva (versao anterior nao arquivada: ArchiveError)
    std::filesystem::path installDir;  // <out>\BlueStinger
    std::filesystem::path exe;         // <out>\BlueStinger\BlueStinger.exe
    std::filesystem::path logFile;     // <out>\.bs-build\builder.log
};

Result build(const Request& req, const Callbacks& cb);

// Instalacao que um build em <outDir>\<installName> substituiria (para os avisos da interface).
struct InstallInfo {
    bool                  exists = false;    // a pasta existe e nao esta vazia
    bool                  ours = false;      // instalada por nos (marca .bs-install)
    bool                  portable = false;  // instalacao.toml pede saves portateis
    std::filesystem::path dir, previous;     // a pasta e onde a versao atual ficara guardada
};
InstallInfo inspect_install(const std::filesystem::path& outDir, const std::wstring& installName = L"BlueStinger");

// Marcas gravadas no pacote instalado: lista dos arquivos do pacote (o resto da pasta e do
// usuario) e o instante da instalacao (identifica backups nossos e o mais recente).
inline constexpr wchar_t kPackageManifest[] = L".bs-package";
inline constexpr wchar_t kInstallMarker[]   = L".bs-install";

struct SwapCancelled : std::runtime_error {
    using std::runtime_error::runtime_error;
};
// A troca falhou ANTES de a nova versao entrar: a anterior continua (codigo do sistema em `code`).
struct SwapError : std::runtime_error {
    std::error_code code;
    SwapError(const std::string& m, std::error_code c) : std::runtime_error(m), code(c) {}
};
// A nova versao JA esta instalada, mas a anterior nao pode ser guardada como <dest>.anterior: a
// mensagem diz onde ela ficou (o build termina com sucesso e um aviso).
struct ArchiveError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Troca segura de pasta (exposta para testes): `stage` passa a ser `dest`.
// - Sem `dest` e com backups `<dest>.old-*` marcados (troca interrompida), o mais recente
//   volta primeiro; com `dest`, backups marcados que sobraram sao apagados.
// - Grava em `stage` a lista do pacote e a marca de instalacao.
// - Arquivos do usuario na versao anterior (tudo fora da lista do pacote anterior; sem
//   lista, fora de isPackageFile) sao copiados para `stage`.
// - `cancel` vale ate o primeiro rename (lanca SwapCancelled); depois, a troca termina.
// - Se a troca falhar, `dest` anterior e restaurado e a funcao lanca (a mensagem diz onde
//   esta a versao anterior se nem a restauracao der certo).
// - Com sucesso, a versao anterior fica em `<dest>.anterior` (rollback: renomear de
//   volta); uma `.anterior` mais antiga, marcada por nos, sai.
void install_swap(const std::filesystem::path& stage, const std::filesystem::path& dest,
                  const std::function<bool(const std::filesystem::path& relative)>& isPackageFile,
                  const std::atomic<bool>* cancel = nullptr);

// Kit de fontes valido? Vazio = sim; senao, o que falta.
std::string check_kit(const std::filesystem::path& kitDir, const std::filesystem::path& sdl3Dir);

// Fontes do toml++ (N3-1: o produto le o arquivo de controles): <kit>\third_party\tomlplusplus
// (distribuicao e projeto editavel) ou, no repositorio, ao lado das fontes do SDL3
// (build\...\_deps\tomlplusplus-src).
std::filesystem::path toml_dir(const std::filesystem::path& kitDir, const std::filesystem::path& sdl3Dir);

} // namespace bs::builder
