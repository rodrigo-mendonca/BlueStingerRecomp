#include "audio/mixer.h"

#include "audio/aica_dsp.h"

#include <algorithm>
#include <cmath>

namespace bs::native::audio {

AudioMixer::AudioMixer(uint32_t outputRate, uint32_t maxVoices)
    : rate_(outputRate ? outputRate : 44100), max_(maxVoices ? maxVoices : 1) {}

// fora de PCM16 depois do arredondamento da saida (lround): a faixa e assimetrica, [-32768, 32767] (revisao Codex
// 2026-10-21: -32768 com ganho 1 e valido)
static bool over16(float x) { return x >= 32767.5f || x <= -32768.5f; }

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

void AudioMixer::release(VoiceId id, double dbPerSecond, double endDb) {
    if (dbPerSecond <= 0) return stop(id);
    for (auto& v : voices_)
        if (v.id == id && !v.releasing) {
            v.releasing = true;
            v.relStep   = dbPerSecond / double(rate_);  // dB por quadro de saida
            v.relMul    = float(std::pow(10.0, -v.relStep / 20.0));
            v.relGain = 1.0f, v.relDb = 0, v.relEnd = endDb;
        }
}

void AudioMixer::set_dsp(std::shared_ptr<AicaDsp> dsp, const std::array<std::array<float, 2>, 16>& outGains) {
    dsp_    = rate_ == 44100 ? std::move(dsp) : nullptr;  // o programa conta amostras de 44 100 Hz (atrasos do anel)
    dspOut_ = outGains;
}

void AudioMixer::pause(VoiceId id, bool paused) {
    for (auto& v : voices_)
        if (v.id == id) v.paused = paused;
}
void AudioMixer::pause_all(bool paused) { allPaused_ = paused; }
void AudioMixer::set_volume(VoiceId id, float volume) {
    for (auto& v : voices_)
        if (v.id == id) v.p.volume = std::clamp(volume, 0.0f, 1.0f), v.rampLeft = 0;
}
void AudioMixer::set_volume_ramp(VoiceId id, float volume, uint32_t frames) {
    for (auto& v : voices_)
        if (v.id == id) {
            const float from = v.rampLeft ? v.rampCur : v.p.volume;
            v.p.volume       = std::clamp(volume, 0.0f, 1.0f);
            v.rampLeft       = frames;
            v.rampCur        = from;
            v.rampStep       = frames ? (v.p.volume - from) / float(frames) : 0.0f;
        }
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
float  AudioMixer::pitch_of(VoiceId id) const {
    for (const auto& v : voices_)
        if (v.id == id) return v.p.pitch;
    return 0.0f;
}

void AudioMixer::render(std::span<int16_t> out) {
    const size_t       frames = out.size() / 2;
    std::vector<float> acc(frames * 2, 0.0f);
    if (dsp_) mixs_.assign(frames * 16, 0.0f);
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
            const float   busG      = bus_[size_t(v.p.bus)];
            const float   panL = std::min(1.0f, 1.0f - v.p.pan), panR = std::min(1.0f, 1.0f + v.p.pan);
            float         vol = v.p.volume * busG, gl = vol * panL, gr = vol * panR;
            bool          done = false;
            const double  span      = double(lEnd - lStart);
            for (size_t f = 0; f < frames; ++f) {
                if (v.rampLeft) {  // rampa de volume (set_volume_ramp): um passo por quadro, ate o alvo
                    v.rampCur += v.rampStep;
                    vol = (--v.rampLeft ? v.rampCur : v.p.volume) * busG, gl = vol * panL, gr = vol * panR;
                }
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
                if (v.releasing) {  // a soltura: ganho exponencial no tempo (linear em dB)
                    l *= v.relGain, r *= v.relGain;
                    v.relGain *= v.relMul, v.relDb += v.relStep;
                }
                const float cl = l * gl, cr = r * gr;
                srcOver_ += over16(cl) || over16(cr);  // uma fonte so acima de 16 bits
                acc[f * 2] += cl;
                acc[f * 2 + 1] += cr;
                if (dsp_ && v.p.dspSend > 0) mixs_[f * 16 + (v.p.dspBus & 15)] += (l + r) * 0.5f * vol * v.p.dspSend;
                if (v.releasing && v.relDb >= v.relEnd) {  // passou do fim do envelope: a voz acaba
                    events_.push_back({v.id, v.p.tag, EventKind::Stopped, frame_ + f + 1});
                    done = true;
                    break;
                }
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
    if (dsp_) {  // o DSP roda tambem com tudo pausado ou sem envio (a cauda da reverberacao continua)
        std::array<int32_t, 16> mixs{};
        std::array<int16_t, 16> ef{};
        for (size_t f = 0; f < frames; ++f) {
            // 16 bits -> MIXS de 20 bits. O somador do hardware nao tem protecao de estouro (manual da AICA: a soma tem de
            // ficar abaixo de 0 dB): passa de 20 bits = volta pelo outro lado, como la (revisao Codex 2026-10-21)
            for (size_t b = 0; b < 16; ++b) {
                const long long w = std::llround(double(mixs_[f * 16 + b]) * 16.0);
                const auto      v = static_cast<uint32_t>(w);
                mixs[b]           = int32_t(v << 12) >> 12;
                dspWraps_ += mixs[b] != w;
            }
            dsp_->sample(mixs, ef);
            for (size_t o = 0; o < 16; ++o) {
                const float el = float(ef[o]) * dspOut_[o][0], er = float(ef[o]) * dspOut_[o][1];
                srcOver_ += over16(el) || over16(er);
                acc[f * 2] += el;
                acc[f * 2 + 1] += er;
            }
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
