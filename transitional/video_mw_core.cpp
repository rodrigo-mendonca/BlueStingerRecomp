#include "video_mw_core.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace bs::transitional::video {

namespace nv = native::video;
namespace na = native::audio;

namespace {
std::mutex  g_prefMutex;
bool        g_prefSet = false;
std::string g_prefLang;
std::string pref_or(const std::string& fallback) {
    std::lock_guard<std::mutex> lock(g_prefMutex);
    return g_prefSet ? g_prefLang : fallback;
}
std::string baseName(std::string_view f) {
    const size_t p = f.find_last_of("\\/");
    return std::string(p == std::string_view::npos ? f : f.substr(p + 1));
}
} // namespace

MwPlayerCore::MwPlayerCore(MwCoreOptions o) : o_(std::move(o)), catalog_(nv::MovieCatalog::load(o_.catalog)) {
    for (const auto& p : catalog_.problems())
        if (o_.log) o_.log("[video] manifesto: " + p);
}
MwPlayerCore::~MwPlayerCore() { stop(); }

int32_t MwPlayerCore::start(std::string_view fname, int64_t now) {
    stop();
    ++stats_.starts;
    const std::string name = baseName(fname);
    entry_                 = catalog_.find(name);
    colorDirty_            = true;  // a faixa de TV depende da origem (Builder ou mod)
    start100ns_ = now, lastPos_ = 0, audibleMax_ = 0, lastSub_.clear();
    if (!entry_ || entry_->absent || entry_->file.empty()) {
        if (o_.log) o_.log("[video] filme sem recurso: " + name + (entry_ && entry_->absent ? " (ausente no disco original)" : " (nao convertido pelo Builder)"));
        return state_ = o_.missingStat;
    }
    play_ = std::make_unique<nv::MoviePlayback>();
    nv::PlaybackOptions po;
    po.end = o_.endRule;
    if (!entry_->mod) {  // recurso do Builder: priming do AAC e fins da fonte original (manifesto)
        po.audioSkipFrames     = entry_->audioDelaySamples;
        po.sourceVideoEnd100ns = entry_->videoEnd100ns;
        po.sourceAudioEnd100ns = entry_->audioEnd100ns;
    }
    std::string err;
    if (!play_->open(entry_->file, po, &err)) {
        if (o_.log) o_.log("[video] " + name + ": " + err);
        play_.reset();
        return state_ = kMwError;
    }
    if (o_.log) o_.log("[video] " + name + " -> " + entry_->file.filename().string() + (entry_->mod ? " (mod)" : ""));
    // audio: saida pedida; sem ela (headless) ou sem dispositivo, a nula
    const auto& f = play_->format();
    if (f.hasAudio) {
        audio_ = o_.makeAudio ? o_.makeAudio() : nullptr;
        if (!audio_ || !audio_->open(f.sampleRate, uint8_t(f.channels)).ok()) {
            if (audio_ && o_.log) o_.log("[video] sem dispositivo de audio: audio do filme descartado");
            audio_ = na::make_null_output();
            audio_->open(f.sampleRate, uint8_t(f.channels));
        }
    }
    // legendas do idioma pedido (opcionais): o idioma exato ("pt-br"), depois o idioma base ("pt");
    // nunca outro idioma por conta propria
    subs_.clear();
    if (const std::string chosen = pref_or(o_.subtitleLang); !chosen.empty()) {
        std::string lang = chosen;
        std::transform(lang.begin(), lang.end(), lang.begin(), [](char ch) { return char(std::tolower(static_cast<unsigned char>(ch))); });
        auto it = entry_->subtitles.find(lang);
        if (it == entry_->subtitles.end() && lang.find('-') != std::string::npos) it = entry_->subtitles.find(lang.substr(0, lang.find('-')));
        if (it != entry_->subtitles.end()) {
            std::vector<std::string> warn;
            if (!nv::load_subtitles(it->second, subs_, &err, &warn) && o_.log) o_.log("[video] " + err);
            for (const auto& w : warn)
                if (o_.log) o_.log("[video] " + w);
            if (!subRenderer_) subRenderer_ = nv::make_subtitle_rasterizer();
        }
    }
    prepLeft_ = std::max(1, o_.prepPolls);
    return state_ = kMwPrep;
}

int32_t MwPlayerCore::status(int64_t now) {
    ++stats_.statusCalls;
    if (state_ != kMwPrep && state_ != kMwPlaying) return state_;
    if (state_ == kMwPrep) {
        if (--prepLeft_ > 0) return state_;
        state_      = kMwPlaying;
        start100ns_ = now;  // o filme comeca a contar quando sai da preparacao
    }
    const int64_t pos = std::max<int64_t>(0, now - start100ns_);
    std::string   err;
    play_->advance(pos, &err);
    feedAudio(pos);
    lastPos_ = pos;
    stats_.framesShown   = play_->frames_shown();
    stats_.framesSkipped = play_->frames_skipped();
    if (!err.empty()) {
        if (o_.log) o_.log("[video] erro de decodificacao: " + err);
        return state_ = kMwError;
    }
    if (play_->finished(pos)) {
        state_ = kMwPlayEnd;
        if (o_.log) o_.log("[video] fim natural: " + std::to_string(pos / 10000) + " ms logicos, " +
                           std::to_string(stats_.framesShown) + " quadros (" + std::to_string(stats_.framesSkipped) + " pulados)");
    }
    return state_;
}

void MwPlayerCore::feedAudio(int64_t pos) {
    if (!audio_) return;
    std::vector<int16_t> pcm = play_->take_audio(pos);  // o priming do AAC ja sai descartado (MoviePlayback)
    if (!audio_->healthy()) {  // dispositivo perdido: segue mudo, sem travar o filme
        if (!stats_.audioLost && o_.log) o_.log("[video] dispositivo de audio perdido: saida nula");
        stats_.audioLost = true;
        const auto& f    = play_->format();
        audio_           = na::make_null_output();
        audio_->open(f.sampleRate, uint8_t(f.channels));
    }
    // o que nao cabe na fila espera a proxima iteracao; se a espera passar de 0,5 s (o audio ficou para
    // tras: host lento, dispositivo travado), o mais antigo e descartado e a fila recomeca na posicao
    const size_t ch = std::max<size_t>(1, play_->format().channels);
    pending_.insert(pending_.end(), pcm.begin(), pcm.end());
    const size_t limit = size_t(play_->format().sampleRate / 2) * ch;
    if (pending_.size() > limit) {
        audio_->flush();
        ++stats_.audioFlushes;
        pending_.erase(pending_.begin(), pending_.end() - ptrdiff_t(limit / 2));
    }
    const size_t n = std::min(pending_.size(), audio_->writable_frames() * ch);
    if (n) {
        if (!audio_->write(std::span<const int16_t>(pending_.data(), n)).ok()) {
            // a saida recusou: mesma politica do dispositivo perdido
            if (!stats_.audioLost && o_.log) o_.log("[video] a saida de audio recusou o bloco: saida nula");
            stats_.audioLost = true;
            const auto& f    = play_->format();
            audio_           = na::make_null_output();
            audio_->open(f.sampleRate, uint8_t(f.channels));
            audio_->write(std::span<const int16_t>(pending_.data(), n));
        }
        pending_.erase(pending_.begin(), pending_.begin() + ptrdiff_t(n));
    }
}

void MwPlayerCore::stop() {
    if (play_ || audio_) ++stats_.stops;
    if (o_.log && (state_ == kMwPrep || state_ == kMwPlaying))  // interrompido pelo jogo (START, por exemplo)
        o_.log("[video] parado pelo jogo em " + std::to_string(lastPos_ / 10000) + " ms logicos");
    if (audio_) audio_->close();
    audio_.reset();
    play_.reset();
    subs_.clear();
    pending_.clear();
    state_ = kMwStop;  // como o mwSfdStop original (8C1582B2 grava 0 no estado), inclusive depois de 3/4
}

int64_t MwPlayerCore::audiblePos() {
    if (!audio_ || !play_) return lastPos_;
    const auto&    f  = play_->format();
    const size_t   ch = std::max<size_t>(1, f.channels);
    const uint64_t q  = audio_->queued_frames() + pending_.size() / ch;  // quadros de audio ainda nao ouvidos
    const int64_t  p  = f.sampleRate ? std::max<int64_t>(0, lastPos_ - int64_t(q * 10000000ull / f.sampleRate)) : lastPos_;
    // nunca volta: quando a fila cresce de uma consulta para a outra a conta recuava alguns ms e a legenda que acabara de
    // sair piscava de volta (NEWGAME, 212 s, medido na janela)
    audibleMax_ = std::max(audibleMax_, p);
    return audibleMax_;
}

void MwPlayerCore::pause(bool p) {
    if (audio_) audio_->pause(p);
}

bool MwPlayerCore::frame(nv::VideoFrame& out) {
    if (!play_ || !play_->has_frame()) return false;
    out = play_->frame();
    // cor do sprite (modelo das opcoes; a faixa de TV so para os filmes do Builder), antes da legenda
    if (colorDirty_) {
        nv::MovieColorModel m = o_.color;
        if (!entry_ || entry_->mod) m.tvRange = false;
        color_.set(m, base_, offset_);
        colorDirty_ = false;
    }
    color_.apply(out.argb);
    if (!subs_.empty() && subRenderer_ && subRenderer_->ok() && !pref_or(o_.subtitleLang).empty()) {
        // a legenda segue o audio que esta saindo do dispositivo, como as falas do gameplay (2026-10-23): a posicao logica
        // menos o que ainda esta na fila da saida e em pending_. Sem isso a linha entrava ate ~200 ms antes da voz (a fila
        // do XAudio2). A saida nula (headless) nao tem fila: nada muda nos testes deterministicos.
        const int64_t audible = audiblePos();
        const std::wstring t = nv::subtitle_at(subs_, audible);
        if (!t.empty()) subRenderer_->draw(out.argb, out.width, out.height, t);
        // Diagnostico BS_VIDEO_LEGENDA_LOG=<arquivo>: cada troca de legenda do filme com a posicao logica e a audivel (ms)
        static FILE* subLog = [] {
            const char* v = std::getenv("BS_VIDEO_LEGENDA_LOG");
            FILE*       f = v && *v ? std::fopen(v, "w") : nullptr;
            if (f) std::fprintf(f, "logico_ms\taudivel_ms\tlegenda\n");
            return f;
        }();
        if (subLog && t != lastSub_) {
            std::string u8;  // UTF-8 (BMP; as legendas nao usam outros planos), quebras de linha como espaco
            for (wchar_t ch : t) {
                const uint32_t v = ch == L'\n' ? ' ' : uint32_t(ch);
                if (v < 0x80) u8 += char(v);
                else if (v < 0x800) u8 += char(0xC0 | (v >> 6)), u8 += char(0x80 | (v & 0x3F));
                else u8 += char(0xE0 | (v >> 12)), u8 += char(0x80 | ((v >> 6) & 0x3F)), u8 += char(0x80 | (v & 0x3F));
            }
            std::fprintf(subLog, "%lld\t%lld\t%s\n", static_cast<long long>(lastPos_ / 10000), static_cast<long long>(audible / 10000),
                         u8.empty() ? "-" : u8.c_str());
            std::fflush(subLog);
            lastSub_ = t;
        }
    }
    return true;
}

void set_subtitle_preference(const std::string& language) {
    std::lock_guard<std::mutex> lock(g_prefMutex);
    g_prefSet = true, g_prefLang = language;
}

} // namespace bs::transitional::video
