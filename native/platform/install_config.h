#pragma once
// Configuracao local de uma instalacao do jogo: instalacao.toml ao lado do BlueStinger.exe, gravado
// pelo recompilador (BlueStinger-Recompiler.exe) e lido pelo proprio jogo ao iniciar. Vale ao abrir o
// executavel direto e continua valendo se a pasta inteira for movida (os caminhos sao relativos ao
// executavel). Hoje: o local dos saves nativos.
//   - documentos (padrao): <Documentos>\DC-Recomp\Blue Stinger\saves (FOLDERID_Documents, que segue
//     redirecionamentos como o OneDrive; nunca o diretorio de trabalho);
//   - portatil: saves\ ao lado do executavel (o mesmo que --saves-portatil).
// Os dois usam os saves nativos (N4); muda so o lugar. Trocar o modo nao move partidas.
#include <filesystem>
#include <string>
#include <system_error>

namespace bs::native::install {

inline constexpr const wchar_t* kConfigFile = L"instalacao.toml";

enum class SavesLocation { Documents, Portable };

struct InstallConfig {
    SavesLocation saves = SavesLocation::Documents;
};

// Le <dir>\instalacao.toml. Ausente: o padrao. Inacessivel, ilegivel ou com valor desconhecido: o
// padrao, com o motivo em *warning (o jogo nao deixa de abrir por causa dele).
InstallConfig read_config(const std::filesystem::path& dir, std::string* warning = nullptr);

// Texto do arquivo (UTF-8) e gravacao atomica (temporario + troca).
std::string config_text(const InstallConfig& c);
bool        write_config(const std::filesystem::path& dir, const InstallConfig& c, std::string* err = nullptr,
                         std::error_code* code = nullptr);

// Pasta dos saves: --saves-pasta > --saves-portatil > instalacao.toml > Documentos. `documents` e a
// pasta Documentos do usuario (vazia se indisponivel: entao `fallback`).
std::filesystem::path saves_root(const std::filesystem::path& cliDir, bool cliPortable, const std::filesystem::path& exeDir,
                                 const InstallConfig& c, const std::filesystem::path& documents,
                                 const std::filesystem::path& fallback);

} // namespace bs::native::install
