#include "audio/mixer.h"

#include <algorithm>
#include <cmath>

namespace bs::native::audio {

AudioMixer::AudioMixer(uint32_t outputRate, uint32_t maxVoices)
    : rate_(outputRate ? outputRate : 44100), max_(maxVoices ? maxVoices : 1) {}

// razao de frequencia aceita: [0.01, 16]; NaN vira 1 (std::clamp devolveria NaN e o indice da fonte seria indefinido)
static float pitch_ok(float p) { return p == p ? std::clamp(p, 0.01f, 16.0f) : 1.0f; }

VoiceId AudioMixer::play(std::shared_ptr<const SoundData> s, const PlayParams& p) {
    if (!s || s->frames() == 0 || s->rate == 0) return 0;
    if (voices_.size() >= max_) {
        // rouba a de menor prioridade (a mais antiga entre as iguais), se a nova nao for menor
        auto victim = std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            return a.p.priority != b.p.priority ? a.p.priority < b.p.priority : a.started < b.started;
        });
        if (victim->p.priority > p.priority) return 0;
        finish(*victim, EventKind::Stolen);
        voices_.erase(victim);
    }
    Voice v;
    v.id      = next_++;
    v.s       = std::move(s);
    v.p       = p;
    v.started = frame_;
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void AudioMixer::finish(Voice& v, EventKind k) { events_.push_back({v.id, v.p.tag, k, frame_}); }

void AudioMixer::stop(VoiceId id) {
    auto it = std::find_if(voices_.begin(), voices_.end(), [id](const Voice& v) { return v.id == id; });
    if (it == voices_.end()) return;
    finish(*it, EventKind::Stopped);
    voices_.erase(it);
}

void AudioMixer::pause(VoiceId id, bool paused) {
    for (auto& v : voices_)
        if (v.id == id) v.paused = paused;
}
void AudioMixer::pause_all(bool paused) { allPaused_ = paused; }
void AudioMixer::set_volume(VoiceId id, float volume) {
    for (auto& v : voices_)
        if (v.id == id) v.p.volume = std::clamp(volume, 0.0f, 1.0f);
}
void AudioMixer::set_pan(VoiceId id, float pan) {
    for (auto& v : voices_)
        if (v.id == id) v.p.pan = std::clamp(pan, -1.0f, 1.0f);
}
void AudioMixer::set_pitch(VoiceId id, float pitch) {
    for (auto& v : voices_)
        if (v.id == id) v.p.pitch = pitch_ok(pitch);
}
void AudioMixer::set_bus_volume(Bus b, float volume) { bus_[size_t(b)] = std::clamp(volume, 0.0f, 1.0f); }
bool AudioMixer::playing(VoiceId id) const {
    return std::any_of(voices_.begin(), voices_.end(), [id](const Voice& v) { return v.id == id; });
}
size_t AudioMixer::active() const { return voices_.size(); }

void AudioMixer::render(std::span<int16_t> out) {
    const size_t       frames = out.size() / 2;
    std::vector<float> acc(frames * 2, 0.0f);
    if (!allPaused_) {
        for (auto it = voices_.begin(); it != voices_.end();) {
            Voice&           v = *it;
            const SoundData& s = *v.s;
            if (v.paused) {
                ++it;
                continue;
            }
            const double  inc       = double(s.rate) / double(rate_) * double(pitch_ok(v.p.pitch));
            const int64_t n         = s.frames();
            const bool    hasPoints = s.loopStart >= 0 && s.loopEnd > s.loopStart && s.loopEnd <= n;
            const int64_t lEnd      = v.p.loop ? (hasPoints ? s.loopEnd : n) : n;
            const int64_t lStart    = v.p.loop ? (hasPoints ? s.loopStart : 0) : 0;
            const float   vol       = v.p.volume * bus_[size_t(v.p.bus)];
            const float   gl = vol * std::min(1.0f, 1.0f - v.p.pan), gr = vol * std::min(1.0f, 1.0f + v.p.pan);
            bool          done = false;
            const double  span      = double(lEnd - lStart);
            for (size_t f = 0; f < frames; ++f) {
                // fim logico (dublagem mais longa): avisa no quadro do original, sem parar o som
                if (!v.logicalSent && v.p.logicalEnd >= 0 && v.pos >= double(v.p.logicalEnd)) {
                    events_.push_back({v.id, v.p.tag, EventKind::LogicalEnd, frame_ + f});
                    v.logicalSent = true;
                }
                // interpolacao linear entre quadros vizinhos da fonte; no fim do laco, o vizinho e o inicio
                const int64_t i0 = std::min(int64_t(v.pos), n - 1);
                const int64_t i1 = i0 + 1 < lEnd ? i0 + 1 : (v.p.loop ? lStart : n - 1);
                const float   t  = float(v.pos - double(i0));
                float         l, r;
                if (s.channels >= 2) {
                    l = s.pcm[size_t(i0) * s.channels] * (1 - t) + s.pcm[size_t(i1) * s.channels] * t;
                    r = s.pcm[size_t(i0) * s.channels + 1] * (1 - t) + s.pcm[size_t(i1) * s.channels + 1] * t;
                } else {
                    l = r = s.pcm[size_t(i0)] * (1 - t) + s.pcm[size_t(i1)] * t;
                }
                acc[f * 2] += l * gl;
                acc[f * 2 + 1] += r * gr;
                v.pos += inc;
                // os fins sao conferidos logo depois de consumir a amostra: um som (ou o fim logico) que acaba no
                // ultimo quadro do bloco avisa neste render, nao no seguinte
                if (!v.logicalSent && v.p.logicalEnd >= 0 && v.pos >= double(v.p.logicalEnd) && v.pos < double(lEnd)) {
                    events_.push_back({v.id, v.p.tag, EventKind::LogicalEnd, frame_ + f + 1});
                    v.logicalSent = true;
                }
                if (v.pos >= double(lEnd)) {
                    if (v.p.loop) {
                        // modulo: um passo maior que o laco inteiro continua dentro dele
                        v.pos = double(lStart) + std::fmod(v.pos - double(lStart), span);
                        events_.push_back({v.id, v.p.tag, EventKind::Looped, frame_ + f + 1});
                    } else {
                        if (!v.logicalSent && v.p.logicalEnd >= 0)  // o fim fisico chegou antes do logico
                            events_.push_back({v.id, v.p.tag, EventKind::LogicalEnd, frame_ + f + 1});
                        events_.push_back({v.id, v.p.tag, EventKind::Finished, frame_ + f + 1});
                        done = true;
                        break;
                    }
                }
            }
            it = done ? voices_.erase(it) : it + 1;
        }
    }
    for (size_t i = 0; i < frames * 2; ++i) out[i] = int16_t(std::clamp(std::lround(acc[i]), -32768L, 32767L));
    frame_ += frames;
}

std::vector<MixerEvent> AudioMixer::take_events(uint64_t upTo) {
    std::vector<MixerEvent> out, keep;
    for (const auto& e : events_) (e.frame <= upTo ? out : keep).push_back(e);
    events_.swap(keep);
    return out;
}

} // namespace bs::native::audio
