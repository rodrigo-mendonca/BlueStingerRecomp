#include "video/movie_playback.h"

#include <algorithm>

namespace bs::native::video {

bool MoviePlayback::open(const std::filesystem::path& file, const PlaybackOptions& o, std::string* err) {
    reader_->close();  // o leitor nao e copiavel: o resto volta ao estado inicial campo a campo
    current_ = next_ = {};
    hasCurrent_ = hasNext_ = videoEof_ = audioEof_ = false;
    videoEnd_ = audioEnd_ = -1;
    shown_ = skipped_ = 0;
    audio_.clear();
    audioQueuedUntil_ = 0;
    o_                = o;
    skipLeft_         = 0;
    if (!reader_->open(file, err)) return false;
    if (!reader_->format().hasAudio) audioEof_ = true, audioEnd_ = 0;
    skipLeft_ = o.audioSkipFrames * int64_t(std::max(1u, reader_->format().channels));
    return true;
}

bool MoviePlayback::advance(int64_t pos, std::string* err) {
    bool changed = false;
    for (;;) {
        if (!hasNext_ && !videoEof_) {
            if (reader_->next_frame(next_, err)) hasNext_ = true;
            else {
                videoEof_ = true;
                videoEnd_ = hasCurrent_ ? current_.time100ns + current_.duration100ns : 0;
            }
        }
        if (!hasNext_ || next_.time100ns > pos) break;
        // o proximo ja chegou: vira o atual (se outro tambem chegar, este foi pulado)
        if (changed) ++skipped_;
        std::swap(current_, next_);
        hasCurrent_ = true, hasNext_ = false, changed = true;
    }
    if (changed) ++shown_;
    return changed;
}

std::vector<int16_t> MoviePlayback::take_audio(int64_t pos, std::string* err) {
    const MovieFormat& f     = reader_->format();
    const int64_t      until = pos + o_.audioLead100ns;
    while (!audioEof_ && audioQueuedUntil_ < until) {
        std::vector<int16_t> pcm;
        int64_t              t = 0;
        if (!reader_->next_audio(pcm, t, err)) {
            audioEof_ = true;
            audioEnd_ = audioQueuedUntil_;
            break;
        }
        size_t from = 0;  // priming do AAC: as primeiras amostras nao sao da fonte
        if (skipLeft_ > 0) {
            from = size_t(std::min<int64_t>(skipLeft_, int64_t(pcm.size())));
            skipLeft_ -= int64_t(from);
        }
        audio_.insert(audio_.end(), pcm.begin() + ptrdiff_t(from), pcm.end());
        audioQueuedUntil_ = t + int64_t(pcm.size() / std::max(1u, f.channels)) * 10'000'000 / std::max(1u, f.sampleRate);
    }
    std::vector<int16_t> out(audio_.begin(), audio_.end());
    audio_.clear();
    return out;
}

bool MoviePlayback::finished(int64_t pos) const {
    // recurso do Builder: os fins da fonte original; mods: os EOFs do proprio arquivo
    const bool v = o_.sourceVideoEnd100ns > 0 ? pos >= o_.sourceVideoEnd100ns : videoEof_ && pos >= videoEnd_;
    const bool a = !reader_->format().hasAudio ? v
                   : o_.sourceAudioEnd100ns > 0 ? pos >= o_.sourceAudioEnd100ns
                                                : audioEof_ && pos >= audioEnd_;
    switch (o_.end) {
    case EndRule::VideoEnd: return v;
    case EndRule::AudioEnd: return reader_->format().hasAudio ? a : v;
    case EndRule::LaterOfBoth: return v && a;
    }
    return v;
}

} // namespace bs::native::video
