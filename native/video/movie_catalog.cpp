#include "video/movie_catalog.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <system_error>

namespace bs::native::video {

namespace fs = std::filesystem;

namespace {

std::string upper(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = char(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

// idioma das legendas sempre em minusculas ("PT-BR" no manifesto = "pt-br" pedido)
std::string lowerLang(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = char(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string stemUpper(std::string_view original) {
    const std::string u   = upper(original);
    const size_t      dot = u.rfind('.');
    return dot == std::string::npos ? u : u.substr(0, dot);
}

bool isFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

// Caminho relativo a pasta do manifesto, sem sair dela: nada absoluto, de unidade ou com "..".
bool safeRelative(const std::string& s) {
    if (s.empty()) return false;
    const fs::path p(std::u8string(s.begin(), s.end()));
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;
    for (const auto& part : p)
        if (part == "..") return false;
    return true;
}

// Depois de resolver juncoes e links: o alvo continua dentro da pasta do manifesto (um caminho
// lexicamente relativo ainda escapa por uma juncao; revisao Codex 2026-10-19).
bool contained(const fs::path& base, const fs::path& target) {
    std::error_code ec;
    const fs::path  b = fs::weakly_canonical(base, ec);
    if (ec) return false;
    const fs::path t = fs::weakly_canonical(target, ec);
    if (ec) return false;
    auto bi = b.begin(), ti = t.begin();
    for (; bi != b.end(); ++bi, ++ti)
        if (ti == t.end() || *bi != *ti) return false;
    return true;
}

// [[filme]] id, original, arquivo, quadros, fps, largura, altura, sha256, legendas = { pt = "..." }
void readManifest(const fs::path& path, bool mod, std::map<uint32_t, MovieEntry>& out, std::vector<std::string>& problems) {
    if (!isFile(path)) return;
    toml::table t;
    try {
        t = toml::parse_file(path.wstring());
    } catch (const toml::parse_error& e) {
        problems.push_back(path.filename().string() + ": " + std::string(e.description()));
        return;
    }
    const auto* list = t["filme"].as_array();
    if (!list) return;
    for (const auto& node : *list) {
        const auto* f = node.as_table();
        if (!f) continue;
        const auto id     = (*f)["id"].value<int64_t>();
        const auto file   = (*f)["arquivo"].value<std::string>();
        const bool absent = (*f)["ausente"].value_or(false);
        if (id && *id >= 0 && absent && !mod) {  // o disco nao tem o filme: so a chave (um mod pode fornece-lo)
            MovieEntry e;
            e.id = uint32_t(*id), e.absent = true;
            if (auto o = (*f)["original"].value<std::string>()) e.original = upper(*o);
            out[e.id] = std::move(e);
            continue;
        }
        if (file && !safeRelative(*file)) {
            problems.push_back(path.filename().string() + ": filme " + (id ? std::to_string(*id) : std::string("?")) +
                               " com caminho fora da pasta (recusado): " + *file);
            continue;
        }
        if (!id || *id < 0 || !file || file->empty()) {
            problems.push_back(path.filename().string() + ": entrada [[filme]] sem id ou arquivo");
            continue;
        }
        MovieEntry e = mod && out.count(uint32_t(*id)) ? out[uint32_t(*id)] : MovieEntry{};
        e.id         = uint32_t(*id);
        if (auto o = (*f)["original"].value<std::string>()) e.original = upper(*o);
        e.file = path.parent_path() / fs::path(std::u8string(file->begin(), file->end()));
        if (!contained(path.parent_path(), e.file)) {
            problems.push_back(path.filename().string() + ": filme " + std::to_string(*id) + " aponta para fora da pasta por juncao ou link (recusado)");
            continue;
        }
        e.mod  = mod;
        e.absent = false;
        if (mod) e.frames = 0, e.width = 0, e.height = 0, e.fps = 0, e.sha256.clear();  // o mod tem os seus
        e.frames = uint32_t((*f)["quadros"].value_or(int64_t(e.frames)));
        e.width  = uint32_t((*f)["largura"].value_or(int64_t(e.width)));
        e.height = uint32_t((*f)["altura"].value_or(int64_t(e.height)));
        e.fps    = (*f)["fps"].value_or(e.fps);
        e.sha256 = (*f)["sha256"].value_or(e.sha256);
        if (!mod) {
            e.audioDelaySamples = int32_t((*f)["atraso_audio_amostras"].value_or(int64_t(0)));
            e.videoEnd100ns     = (*f)["fim_video_100ns"].value_or(int64_t(0));
            e.audioEnd100ns     = (*f)["fim_audio_100ns"].value_or(int64_t(0));
        } else e.audioDelaySamples = int32_t((*f)["atraso_audio_amostras"].value_or(int64_t(0)));
        if (const auto* subs = (*f)["legendas"].as_table())
            for (const auto& [lang, v] : *subs)
                if (auto s = v.value<std::string>()) {
                    const fs::path sp = path.parent_path() / fs::path(std::u8string(s->begin(), s->end()));
                    if (safeRelative(*s) && contained(path.parent_path(), sp)) e.subtitles[lowerLang(lang.str())] = sp;
                    else problems.push_back(path.filename().string() + ": legenda com caminho fora da pasta (recusada): " + *s);
                }
        if (!isFile(e.file)) problems.push_back(path.filename().string() + ": filme " + std::to_string(e.id) + " sem o arquivo " + e.file.filename().string());
        out[e.id] = std::move(e);
    }
}

} // namespace

MovieCatalog MovieCatalog::load(const CatalogOptions& o) {
    MovieCatalog c;
    readManifest(o.dataDir / "manifest.toml", false, c.byId_, c.problems_);
    if (o.modsDir.empty()) return c;
    readManifest(o.modsDir / "manifest.toml", true, c.byId_, c.problems_);
    // mod pelo nome do original (sem manifesto): mods\video\<NOME>.mp4 e <NOME>.<idioma>.srt/.vtt
    for (auto& [id, e] : c.byId_) {
        if (e.original.empty()) continue;
        const std::string stem = stemUpper(e.original);
        std::error_code   ec;
        for (const auto& f : fs::directory_iterator(o.modsDir, ec)) {
            if (!f.is_regular_file(ec)) continue;
            const std::string name = upper(f.path().filename().string());
            if (!e.mod && name == stem + ".MP4") {
                e.file = f.path(), e.mod = true, e.absent = false, e.audioDelaySamples = 0;
                e.frames = e.width = e.height = 0, e.fps = 0, e.sha256.clear();
            }
            for (const char* ext : {".SRT", ".VTT"}) {  // NOME.<idioma>.srt
                const std::string pre = stem + ".";
                if (name.size() > pre.size() + 4 && name.rfind(pre, 0) == 0 && name.ends_with(ext)) {
                    std::string lang = f.path().filename().string().substr(pre.size());
                    lang             = lang.substr(0, lang.size() - 4);
                    std::transform(lang.begin(), lang.end(), lang.begin(), [](char ch) { return char(std::tolower(static_cast<unsigned char>(ch))); });
                    if (!lang.empty() && lang.find('.') == std::string::npos) e.subtitles[lang] = f.path();
                }
            }
        }
    }
    return c;
}

const MovieEntry* MovieCatalog::find(uint32_t id) const {
    const auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &it->second;
}

const MovieEntry* MovieCatalog::find(std::string_view original) const {
    const std::string u = upper(original);
    for (const auto& [id, e] : byId_)
        if (e.original == u) return &e;
    return nullptr;
}

} // namespace bs::native::video
