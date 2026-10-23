#include "platform/install_config.h"

#include <toml++/toml.hpp>

#include <cctype>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace bs::native::install {

InstallConfig read_config(const fs::path& dir, std::string* warning) {
    InstallConfig  c;
    const fs::path p = dir / kConfigFile;
    std::error_code ec;
    const bool      exists = fs::exists(p, ec);
    if (ec) {  // nem consultar deu: avisar, nao fingir que nao existe
        if (warning) *warning = "instalacao.toml inacessivel (" + ec.message() + "); usando documentos";
        return c;
    }
    if (!exists) return c;
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) {
        if (warning) *warning = "instalacao.toml sem permissao de leitura; usando documentos";
        return c;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    if (in.bad()) {
        if (warning) *warning = "instalacao.toml ilegivel (falha de leitura); usando documentos";
        return c;
    }
    try {
        const toml::table t = toml::parse(ss.str());
        if (const auto v = t["legendas"]["idioma"].value<std::string>()) {
            if (!parse_subtitles(*v, &c.subtitles) && warning)
                *warning = "instalacao.toml: legendas.idioma = \"" + *v + "\" invalido; usando " + kDefaultSubtitles;
        }
        if (const auto v = t["saves"]["local"].value<std::string>()) {
            if (*v == "portatil") c.saves = SavesLocation::Portable;
            else if (*v != "documentos" && warning)
                *warning = "instalacao.toml: saves.local = \"" + *v + "\" desconhecido; usando documentos";
        }
    } catch (const toml::parse_error& e) {
        if (warning) *warning = std::string("instalacao.toml ilegivel (") + std::string(e.description()) + "); usando documentos";
    }
    return c;
}

std::string config_text(const InstallConfig& c) {
    return std::string("# Blue Stinger: configuracao desta instalacao (gravada pelo BlueStinger-Recompiler).\n"
                       "# saves.local: \"documentos\" = Documentos\\DC-Recomp\\Blue Stinger\\saves;\n"
                       "#              \"portatil\"   = pasta saves ao lado do BlueStinger.exe.\n"
                       "# Trocar o valor nao move as partidas de um lugar para o outro.\n"
                       "[saves]\nlocal = \"") +
           (c.saves == SavesLocation::Portable ? "portatil" : "documentos") + "\"\n" +
           "\n# legendas.idioma: \"auto\" (padrao: pt-BR se instalado, senao en-US), um idioma (pt-BR, en-US...) ou \"nenhuma\".\n"
           "# As legendas vem de mods\n"
           "# (mods\\video, mods\\<pacote>\\legendas); sem legendas no idioma, nada aparece.\n"
           "[legendas]\nidioma = \"" + (c.subtitles.empty() ? std::string("nenhuma") : c.subtitles) + "\"\n";
}

bool parse_subtitles(const std::string& v, std::string* out) {
    if (v == "nenhuma" || v == "off" || v == "nao" || v == "none") return *out = "", true;
    if (v == "auto" || v == "automatico") return *out = "auto", true;
    if (v.empty() || v.size() > 35 || !std::isalpha(static_cast<unsigned char>(v[0]))) return false;
    for (char ch : v)
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-') return false;
    *out = v;
    return true;
}

bool write_config(const fs::path& dir, const InstallConfig& c, std::string* err, std::error_code* code) {
    const fs::path p = dir / kConfigFile, tmp = dir / (std::wstring(kConfigFile) + L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << config_text(c);
        const int e = errno;
        out.close();
        if (!out) {
            if (code) *code = std::error_code(e ? e : EIO, std::generic_category());
            if (err) *err = "nao foi possivel gravar " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        if (code) *code = ec;
        std::error_code ignore;
        fs::remove(tmp, ignore);
        if (err) *err = "nao foi possivel gravar " + p.string() + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

fs::path saves_root(const fs::path& cliDir, bool cliPortable, const fs::path& exeDir, const InstallConfig& c,
                    const fs::path& documents, const fs::path& fallback) {
    if (!cliDir.empty()) return cliDir;
    if (cliPortable || c.saves == SavesLocation::Portable) return exeDir / L"saves";
    if (!documents.empty()) return documents / L"DC-Recomp" / L"Blue Stinger" / L"saves";
    return fallback;
}

} // namespace bs::native::install
