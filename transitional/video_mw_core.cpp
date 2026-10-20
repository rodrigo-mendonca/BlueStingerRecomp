#include "video_mw_core.h"

#include <algorithm>

namespace bs::transitional::video {

namespace nv = native::video;
namespace na = native::audio;

namespace {
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
    start100ns_ = now, lastPos_ = 0;
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
    if (!o_.subtitleLang.empty()) {
        std::string lang = o_.subtitleLang;
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
    if (!subs_.empty() && subRenderer_ && subRenderer_->ok()) {
        const std::wstring t = nv::subtitle_at(subs_, lastPos_);
        if (!t.empty()) subRenderer_->draw(out.argb, out.width, out.height, t);
    }
    return true;
}

} // namespace bs::transitional::video
