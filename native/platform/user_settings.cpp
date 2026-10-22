#include "platform/user_settings.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace bs::native::settings {
namespace {

bool parse_doc(const std::string& text, toml::table& doc, const char* file, std::string* warning) {
    try {
        doc = toml::parse(text);
        return true;
    } catch (const toml::parse_error& e) {
        if (warning) *warning = std::string(file) + " ilegivel (" + std::string(e.description()) + "): padrao";
        return false;
    }
}

uint32_t pct(const toml::table& doc, const char* key, uint32_t def, uint32_t lo, uint32_t hi) {
    if (auto v = doc[key].value<int64_t>()) return uint32_t(std::clamp<int64_t>(*v, lo, hi));
    return def;
}

// etiqueta de idioma simples: letras, digitos e '-', comecando por letra, ate 35 caracteres
bool language_ok(const std::string& s) {
    if (s.empty() || s.size() > 35 || !std::isalpha(static_cast<unsigned char>(s[0]))) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; });
}

std::string read_file(const fs::path& p, bool* present) {
    std::ifstream in(p, std::ios::binary);
    if (present) *present = bool(in);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

std::string to_toml(const AudioSettings& s) {
    std::ostringstream o;
    o << "# Blue Stinger Native PC: volumes (0 a 100), gravados pelo menu do jogo\n"
      << "geral = " << s.master << "\nmusica = " << s.music << "\nefeitos = " << s.effects << "\nvozes = " << s.voices << "\n";
    return o.str();
}

AudioSettings audio_from_toml(const std::string& text, std::string* warning) {
    AudioSettings s;
    toml::table   doc;
    if (!parse_doc(text, doc, "audio.toml", warning)) return s;
    s.master = pct(doc, "geral", 100, 0, 100), s.music = pct(doc, "musica", 100, 0, 100);
    s.effects = pct(doc, "efeitos", 100, 0, 100), s.voices = pct(doc, "vozes", 100, 0, 100);
    return s;
}

std::string to_toml(const SubtitleSettings& s) {
    std::ostringstream o;
    o << "# Blue Stinger Native PC: legendas (gravadas pelo menu do jogo)\n"
      << "ligadas = " << (s.enabled ? "true" : "false") << "\nidioma = \"" << s.language << "\"\ntamanho = " << s.size_pct
      << "\ncontorno = " << (s.outline ? "true" : "false") << "\nfundo = " << (s.box ? "true" : "false") << "\n";
    return o.str();
}

SubtitleSettings subtitles_from_toml(const std::string& text, std::string* warning) {
    SubtitleSettings s;
    toml::table      doc;
    if (!parse_doc(text, doc, "legendas.toml", warning)) return s;
    if (auto v = doc["ligadas"].value<bool>()) s.enabled = *v;
    if (auto v = doc["idioma"].value<std::string>()) {
        if (language_ok(*v)) s.language = *v;
        else if (warning) *warning = "legendas.toml: idioma invalido \"" + *v + "\" (padrao en-US)";
    }
    s.size_pct = pct(doc, "tamanho", 100, 50, 200);
    if (auto v = doc["contorno"].value<bool>()) s.outline = *v;
    if (auto v = doc["fundo"].value<bool>()) s.box = *v;
    if (!s.outline && !s.box) s.outline = true;  // sem nenhum dos dois o texto branco some em cenas claras
    return s;
}

AudioSettings read_audio(const fs::path& dir, std::string* warning, bool* present) {
    const std::string t = read_file(dir / kAudioFile, present);
    return t.empty() ? AudioSettings{} : audio_from_toml(t, warning);
}

SubtitleSettings read_subtitles(const fs::path& dir, std::string* warning, bool* present) {
    const std::string t = read_file(dir / kSubtitlesFile, present);
    return t.empty() ? SubtitleSettings{} : subtitles_from_toml(t, warning);
}

bool write_text_atomic(const fs::path& p, const std::string& text, std::string* err, std::error_code* code) {
    const fs::path tmp = p.parent_path() / (p.filename().wstring() + L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
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

bool write_audio(const fs::path& dir, const AudioSettings& s, std::string* err) {
    return write_text_atomic(dir / kAudioFile, to_toml(s), err);
}
bool write_subtitles(const fs::path& dir, const SubtitleSettings& s, std::string* err) {
    return write_text_atomic(dir / kSubtitlesFile, to_toml(s), err);
}

} // namespace bs::native::settings
