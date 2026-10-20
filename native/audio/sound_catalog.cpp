#include "audio/sound_catalog.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;

namespace bs::native::audio {

namespace {

fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = char(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::optional<SoundKind> kindOf(std::string_view id) {
    if (id.rfind("voz/", 0) == 0) return SoundKind::Voice;
    if (id.rfind("musica/", 0) == 0) return SoundKind::Music;
    if (id.rfind("efeito/", 0) == 0) return SoundKind::Effect;
    return std::nullopt;
}

// o arquivo tem que ficar dentro da pasta (sem .., sem caminho absoluto, sem juncao para fora). Devolve o
// caminho canonico conferido (vazio se fora): e ele que se guarda e se abre, sem reconsultar o caminho
// original (revisao Codex 2026-10-20, TOCTOU com juncoes).
fs::path contained(const fs::path& dir, const fs::path& file) {
    std::error_code ec;
    const fs::path  d = fs::weakly_canonical(dir, ec);
    if (ec) return {};
    const fs::path f = fs::weakly_canonical(file, ec);
    if (ec) return {};
    const auto rel = f.lexically_relative(d);
    if (rel.empty() || rel.native()[0] == L'.' || rel.is_absolute()) return {};
    return f;
}

std::string fileName(std::string_view id) {  // ':' -> '#' (nome valido no Windows)
    std::string r(id);
    for (char& c : r)
        if (c == ':') c = '#';
    return r;
}

std::string readAll(const fs::path& p) {
    std::ifstream     in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

bool SoundCatalog::valid_id(std::string_view id) {
    if (!kindOf(id) || id.size() > 128 || id.find("..") != std::string_view::npos) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '_' || c == '-' || c == ':' || c == '.';  // '#' fica de fora: e o escape do ':'
    });
}

SoundCatalog SoundCatalog::load(const SoundCatalogOptions& o) {
    SoundCatalog c;
    c.o_ = o;
    std::error_code ec;
    const fs::path  man = o.dataDir / "manifest.toml";
    if (fs::is_regular_file(man, ec)) {
        try {
            const toml::table t = toml::parse(readAll(man));
            if (const auto* arr = t["som"].as_array())
                for (const auto& n : *arr) {
                    const auto* e  = n.as_table();
                    const auto  id = e ? (*e)["id"].value<std::string>() : std::nullopt;
                    const auto  f  = e ? (*e)["arquivo"].value<std::string>() : std::nullopt;
                    if (!id || !valid_id(*id) || !f) {
                        c.problems_.push_back("manifest.toml: entrada sem id valido ou arquivo");
                        continue;
                    }
                    SoundEntry s;
                    s.id              = *id;
                    s.kind            = *kindOf(*id);
                    s.file            = o.dataDir / fromUtf8(*f);
                    s.originalSeconds = (*e)["duracao_s"].value_or(0.0);
                    s.file            = contained(o.dataDir, s.file);
                    if (s.file.empty()) {
                        c.problems_.push_back("manifest.toml: " + *id + " fora da pasta de dados");
                        continue;
                    }
                    c.original_[s.id] = s;
                }
        } catch (const toml::parse_error& e) {
            c.problems_.push_back(std::string("manifest.toml ilegivel: ") + std::string(e.description()));
        }
    }
    const fs::path modman = o.modsDir / "manifest.toml";
    if (!o.modsDir.empty() && fs::is_regular_file(modman, ec)) {
        try {
            const toml::table t = toml::parse(readAll(modman));
            if (const auto* arr = t["som"].as_array())
                for (const auto& n : *arr) {
                    const auto* e    = n.as_table();
                    const auto  id   = e ? (*e)["id"].value<std::string>() : std::nullopt;
                    const auto  sync = e ? (*e)["sincronia"].value<std::string>() : std::nullopt;
                    if (!id || !valid_id(*id)) continue;
                    if (sync && *sync != "original" && *sync != "arquivo") {
                        c.problems_.push_back("mods: " + *id + ": sincronia \"" + *sync + "\" desconhecida (original ou arquivo)");
                        continue;
                    }
                    c.modSync_[*id] = sync && *sync == "arquivo" ? SyncPolicy::File : SyncPolicy::Original;
                }
        } catch (const toml::parse_error& e) {
            c.problems_.push_back(std::string("mods\\audio\\manifest.toml ilegivel: ") + std::string(e.description()));
        }
    }
    return c;
}

std::optional<SoundEntry> SoundCatalog::resolve(std::string_view id, std::string_view lang) const {
    if (!valid_id(id)) return std::nullopt;
    const auto  orig = original_.find(std::string(id));
    std::string l    = lower(lang);
    std::vector<std::string> langs;
    if (!l.empty()) {
        langs.push_back(l);
        if (const size_t dash = l.find('-'); dash != std::string::npos) langs.push_back(l.substr(0, dash));
    }
    langs.push_back("");  // mod neutro
    if (!o_.modsDir.empty())
        for (const auto& lg : langs)
            for (const char* ext : {".wav", ".flac", ".ogg"}) {
                const fs::path base = lg.empty() ? o_.modsDir : o_.modsDir / fromUtf8(lg);
                const fs::path  f = base / fromUtf8(fileName(id) + ext);
                std::error_code ec;
                if (!fs::is_regular_file(f, ec)) continue;
                const fs::path canon = contained(o_.modsDir, f);
                if (canon.empty()) continue;
                SoundEntry s;
                s.id   = std::string(id);
                s.kind = *kindOf(id);
                s.file = canon;
                s.mod  = true;
                s.lang = lg;
                s.originalSeconds = orig != original_.end() ? orig->second.originalSeconds : 0;
                const auto ms     = modSync_.find(s.id);
                s.sync            = ms != modSync_.end() ? ms->second : SyncPolicy::Original;
                return s;
            }
    if (orig != original_.end()) return orig->second;
    return std::nullopt;
}

int64_t logical_end_frames(const SoundEntry& e, int64_t fileFrames, uint32_t fileRate, uint32_t rate) {
    const int64_t file = fileRate ? fileFrames * int64_t(rate) / int64_t(fileRate) : 0;
    if (e.sync == SyncPolicy::Original && e.originalSeconds > 0) return int64_t(e.originalSeconds * rate + 0.5);
    return file;
}

} // namespace bs::native::audio
