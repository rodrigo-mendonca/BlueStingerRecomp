#include "localization/subtitle_service.h"

#include "localization/locale.h"
#include "video/subtitles.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>

namespace bs::native::localization {

namespace {
std::wstring widen_utf8(const std::string& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t            cp;
        size_t              n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
        cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
        for (size_t k = 1; k <= n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n + 1;
        if (cp >= 0x10000) {  // UTF-16 (wchar_t do Windows)
            cp -= 0x10000;
            out += wchar_t(0xD800 + (cp >> 10)), out += wchar_t(0xDC00 + (cp & 0x3FF));
        } else {
            out += wchar_t(cp);
        }
    }
    return out;
}
int64_t seconds(double s) { return int64_t(std::llround(s * 1e7)); }
} // namespace

SubtitleService::SubtitleService(const ModPackages& mods, std::string language) : mods_(mods), lang_(canonical_language(language)) {
    if (lang_.empty()) return;  // sem idioma valido: legendas desligadas
    const auto f = mods_.find("legendas/falas.{idioma}.toml", lang_);
    if (!f) return;
    toml::table t;
    try {
        t = toml::parse_file(f->file.u8string());
    } catch (const toml::parse_error& e) {
        problems_.push_back(f->package->id + ": falas ilegivel: " + std::string(e.description()));
        return;
    }
    if (const auto* arr = t["fala"].as_array()) {
        for (const auto& n : *arr) {
            const auto* e = n.as_table();
            if (!e) continue;
            const std::string voz = (*e)["voz"].value_or(std::string()), cena = (*e)["cena"].value_or(std::string()),
                              texto = (*e)["texto"].value_or(std::string());
            const double ini = (*e)["inicio"].value_or(0.0), fim = (*e)["fim"].value_or(-1.0);
            if (!valid_resource_id(voz) || (!cena.empty() && !valid_resource_id(cena))) {
                problems_.push_back(f->package->id + ": fala com voz/cena invalida '" + voz + "'");
                continue;
            }
            if (!valid_utf8(texto) || texto.empty() || ini < 0 || (fim >= 0 && fim <= ini)) {
                problems_.push_back(f->package->id + ": fala de '" + voz + "' com texto ou tempos invalidos");
                continue;
            }
            byVoiceScene_[voz + "|" + cena].push_back({seconds(ini), fim < 0 ? -1 : seconds(fim), widen_utf8(texto)});
        }
    }
    for (auto& [k, v] : byVoiceScene_) std::stable_sort(v.begin(), v.end(), [](const TimedLine& a, const TimedLine& b) { return a.start100ns < b.start100ns; });
}

void SubtitleService::movie_started(const std::string& name) {
    movieLines_.clear(), movie_ = name, movieTime_ = 0, movieOn_ = !lang_.empty();
    if (!movieOn_) return;
    for (const char* ext : {".srt", ".vtt"}) {
        const auto f = mods_.find("legendas/" + name + ".{idioma}" + ext, lang_);
        if (!f) continue;
        std::vector<video::SubtitleCue> cues;
        std::string                     err;
        std::vector<std::string>        warn;
        if (!video::load_subtitles(f->file, cues, &err, &warn)) {
            problems_.push_back(f->package->id + ": " + name + ": " + err);
            continue;
        }
        for (auto& w : warn) problems_.push_back(f->package->id + ": " + name + ": " + w);
        for (auto& c : cues) movieLines_.push_back({c.start100ns, c.end100ns, std::move(c.text)});
        break;
    }
}
void SubtitleService::movie_time(int64_t t) { movieTime_ = t; }
void SubtitleService::movie_ended() { movieOn_ = false, movieLines_.clear(); }
void SubtitleService::movie_skipped() { movie_ended(); }

void SubtitleService::dialogue_started(const std::string& voice, const std::string& scene, int64_t t) {
    now_ = t;
    dialogue_ended(voice);  // a mesma voz de novo recomeca (evento repetido nao duplica)
    auto it = byVoiceScene_.find(voice + "|" + scene);
    if (it == byVoiceScene_.end()) it = byVoiceScene_.find(voice + "|");
    if (it == byVoiceScene_.end()) return;  // sem legenda para esta fala: nada (original)
    dialogues_.push_back({voice, &it->second, t, -1, 0});
}
void SubtitleService::dialogue_ended(const std::string& voice) {
    dialogues_.erase(std::remove_if(dialogues_.begin(), dialogues_.end(), [&](const Dialogue& d) { return d.voice == voice; }), dialogues_.end());
}
void SubtitleService::dialogue_interrupted(const std::string& voice) { dialogue_ended(voice); }
void SubtitleService::logical_time(int64_t t) { now_ = t; }
void SubtitleService::paused(bool on, int64_t t) {
    now_ = t;
    for (auto& d : dialogues_) {
        if (on && d.pausedAt < 0) d.pausedAt = t;
        else if (!on && d.pausedAt >= 0) d.pausedTotal += t - d.pausedAt, d.pausedAt = -1;
    }
}
void SubtitleService::scene_changed(const std::string&) { dialogues_.clear(); }

int64_t SubtitleService::elapsed(const Dialogue& d) const {
    const int64_t until = d.pausedAt >= 0 ? d.pausedAt : now_;
    return std::max<int64_t>(0, until - d.started - d.pausedTotal);
}

std::vector<ActiveSubtitle> SubtitleService::active() const {
    std::vector<ActiveSubtitle> out;
    if (movieOn_)
        for (const auto& l : movieLines_)
            if (movieTime_ >= l.start100ns && (l.end100ns < 0 || movieTime_ < l.end100ns)) out.push_back({ActiveSubtitle::Source::Movie, movie_, l.text});
    for (const auto& d : dialogues_) {
        const int64_t e = elapsed(d);
        for (const auto& l : *d.lines)
            if (e >= l.start100ns && (l.end100ns < 0 || e < l.end100ns)) out.push_back({ActiveSubtitle::Source::Dialogue, d.voice, l.text});
    }
    return out;
}

} // namespace bs::native::localization
