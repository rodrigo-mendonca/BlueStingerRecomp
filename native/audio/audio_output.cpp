// N5 (D2): saidas de audio atras de IAudioOutput. XAudio2 com uma voz de origem PCM 16 bits e uma fila
// de blocos proprios (o XAudio2 le da memoria de quem enviou ate o fim do bloco); nula para o headless.
#include "audio/audio.h"
#include "core/com_scope.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace bs::native::audio {

namespace {

// Identificador da saida padrao do Windows (vazio: nenhuma), para notar a troca com o jogo aberto.
std::wstring defaultRenderDeviceId() {
    std::wstring       out;
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&en))))
        return out;
    IMMDevice* dev = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) && dev) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(dev->GetId(&id)) && id) out = id, CoTaskMemFree(id);
        dev->Release();
    }
    en->Release();
    return out;
}

class NullOutput final : public IAudioOutput {
public:
    Result<void> open(uint32_t, uint8_t ch) override {
        channels_ = ch ? ch : 1;
        return {};
    }
    size_t       writable_frames() const override { return paused_ ? 0 : size_t(1) << 20; }
    Result<void> write(std::span<const int16_t> pcm) override {
        if (!paused_) played_ += pcm.size() / channels_;
        return {};
    }
    void     close() override {}
    void     pause(bool p) override { paused_ = p; }
    void     flush() override {}
    uint64_t played_frames() const override { return played_; }
    bool     healthy() const override { return true; }

private:
    uint32_t channels_ = 1;
    uint64_t played_   = 0;
    bool     paused_   = false;
};

class XAudio2Output final : public IAudioOutput, IXAudio2EngineCallback {
public:
    XAudio2Output(uint32_t latencyMs, bool follow, XAudio2Hooks hooks = {})
        : latencyMs_(latencyMs), follow_(follow), hooks_(std::move(hooks)) {}
    ~XAudio2Output() override { close(); }

    Result<void> open(uint32_t rate, uint8_t ch) override {
        close();
        // estado por sessao: uma reabertura comeca do zero (revisao Codex 2026-10-21)
        primed_ = paused_ = false, lost_ = false, stats_ = {}, playedBase_ = 0;
        return start(rate, ch);
    }

    // motor, voz de mestre (a saida padrao atual) e voz de origem; o estado da sessao fica
    Result<void> start(uint32_t rate, uint8_t ch) {
        if (follow_) deviceId_ = deviceId(), lastCheck_ = nowMs();
        if (FAILED(XAudio2Create(&x_, 0, XAUDIO2_DEFAULT_PROCESSOR))) return Error::Unsupported;
        x_->RegisterForCallbacks(this);
        if (FAILED(x_->CreateMasteringVoice(&master_))) {  // sem dispositivo de saida
            close();
            return Error::NotFound;
        }
        WAVEFORMATEX& f = fmt_;
        f               = {};
        f.wFormatTag      = WAVE_FORMAT_PCM;
        f.nChannels       = ch;
        f.nSamplesPerSec  = rate;
        f.wBitsPerSample  = 16;
        f.nBlockAlign     = WORD(ch * 2);
        f.nAvgBytesPerSec = rate * f.nBlockAlign;
        if (FAILED(x_->CreateSourceVoice(&voice_, &f))) {
            close();
            return Error::Unsupported;
        }
        channels_ = ch, rate_ = rate;
        if (!paused_) voice_->Start();  // reabertura durante a pausa: a voz nova fica parada (revisao Codex 2026-10-21)
        return {};
    }

    size_t writable_frames() const override {
        if (!voice_ || lost_) return 0;
        const_cast<XAudio2Output*>(this)->reclaim();
        const uint64_t cap = uint64_t(rate_) * latencyMs_ / 1000;
        return queuedFrames_ >= cap ? 0 : size_t(cap - queuedFrames_);
    }

    Result<void> write(std::span<const int16_t> pcm) override {
        if (follow_) followDefault();
        if (!voice_ || lost_) return ++stats_.writeErrors, Error::Io;
        if (pcm.empty()) return {};
        reclaim();
        // fila do XAudio2 no limite (o jogo recupera campos atrasados de uma vez): descarta o bloco, o dispositivo segue.
        // O teto e o latency_ms do contrato, contando o bloco novo (e a reserva, se for entrar): a fila nunca passa dele
        // (revisao Codex 2026-10-21; antes, 4x)
        const uint64_t novo = pcm.size() / channels_ + (blocks_.empty() ? uint64_t(rate_) * kCushionMs / 1000 : 0);
        if (follow_ && (blocks_.size() >= XAUDIO2_MAX_QUEUED_BUFFERS - 2 ||
                        queuedFrames_ + novo > uint64_t(rate_) * latencyMs_ / 1000)) {
            ++stats_.dropped;
            return {};
        }
        // fila vazia depois do primeiro bloco (fora de pausa e de descarte): o dispositivo ficou sem dados
        if (primed_ && blocks_.empty() && !paused_) ++stats_.underruns;
        // audio do jogo: com a fila vazia, a reserva de silencio antes do bloco (a folga para os campos atrasados)
        if (follow_ && blocks_.empty()) {
            blocks_.emplace_back(size_t(uint64_t(rate_) * kCushionMs / 1000) * channels_, int16_t(0));
            XAUDIO2_BUFFER c{};
            c.AudioBytes = UINT32(blocks_.back().size() * 2);
            c.pAudioData = reinterpret_cast<const BYTE*>(blocks_.back().data());
            if (FAILED(voice_->SubmitSourceBuffer(&c))) {
                blocks_.pop_back();
                lost_ = true;
                ++stats_.writeErrors;
                return Error::Io;
            }
            queuedFrames_ += blocks_.back().size() / channels_;
            ++stats_.cushions;
        }
        blocks_.emplace_back(pcm.begin(), pcm.end());
        XAUDIO2_BUFFER b{};
        b.AudioBytes = UINT32(blocks_.back().size() * 2);
        b.pAudioData = reinterpret_cast<const BYTE*>(blocks_.back().data());
        if (FAILED(voice_->SubmitSourceBuffer(&b))) {  // a voz nao aceita mais: tratado como dispositivo perdido
            blocks_.pop_back();
            lost_ = true;
            ++stats_.writeErrors;
            return Error::Io;
        }
        queuedFrames_ += pcm.size() / channels_;
        primed_ = true, ++stats_.blocks, stats_.maxQueuedFrames = std::max(stats_.maxQueuedFrames, queuedFrames_);
        return {};
    }

    void close() override {
        lastPlayed_ = 0;
        if (voice_) voice_->DestroyVoice(), voice_ = nullptr;
        if (master_) master_->DestroyVoice(), master_ = nullptr;
        if (x_) x_->Release(), x_ = nullptr;
        blocks_.clear();
        queuedFrames_ = 0;
    }

    void pause(bool p) override {
        if (!voice_) return;
        paused_ = p;
        p ? voice_->Stop() : voice_->Start();
    }

    // Descarte da fila: a voz e destruida (DestroyVoice e sincrono: depois dele o XAudio2 nao le mais
    // nenhum bloco) e recriada; so entao os blocos saem. O FlushSourceBuffers nao garante isso a tempo
    // (revisao Codex 2026-10-19).
    void flush() override {
        if (!voice_) return;
        playedBase_ += currentPlayed();
        lastPlayed_ = 0;
        voice_->DestroyVoice();
        voice_ = nullptr;
        blocks_.clear();
        queuedFrames_ = 0;
        primed_       = false;  // a fila vazia depois do descarte e pedida, nao falta
        if (FAILED(x_->CreateSourceVoice(&voice_, &fmt_))) {
            voice_ = nullptr;
            lost_  = true;
            return;
        }
        if (!paused_) voice_->Start();  // descarte durante a pausa: a voz nova continua parada
    }

    uint64_t played_frames() const override { return playedBase_ + currentPlayed(); }

    // seguindo a saida padrao, a saida nunca desiste: sem dispositivo agora, tenta de novo a cada segundo
    bool healthy() const override { return follow_ || (voice_ && !lost_); }
    Stats stats() const override {
        Stats s = stats_;
        s.lost  = lost_;
        return s;
    }

private:
    // A cada segundo: a saida padrao mudou, ou o dispositivo caiu (OnCriticalError)? Reabre na saida padrao atual,
    // somando os tocados das vozes antigas (played_frames continua crescendo) e mantendo as estatisticas.
    std::wstring deviceId() const { return hooks_.device_id ? hooks_.device_id() : defaultRenderDeviceId(); }
    uint64_t     nowMs() const { return hooks_.now_ms ? hooks_.now_ms() : GetTickCount64(); }
    void followDefault() {
        if (hooks_.force_lost && hooks_.force_lost()) lost_ = true;
        const uint64_t now = nowMs();
        if (now - lastCheck_ < 1000) return;
        lastCheck_ = now;
        const std::wstring id = deviceId();
        if (!lost_ && voice_ && id == deviceId_) return;
        if (id.empty()) return;  // nenhuma saida agora: tenta de novo no proximo segundo
        // os tocados da voz antiga: o ultimo valor lido com o motor saudavel (depois de um erro critico o XAudio2 nao
        // responde mais; revisao Codex 2026-10-21), entao played_frames() nunca regride
        playedBase_ += currentPlayed();
        const Stats keep = stats_;
        close();
        lost_   = false;
        primed_ = false;
        const bool ok = static_cast<bool>(start(rate_, uint8_t(channels_)));
        stats_ = keep;
        ++stats_.reopens;
        if (!ok) lost_ = true;
    }
    // tocados pela voz atual; com o motor perdido, o ultimo valor lido (o cursor congela)
    uint64_t currentPlayed() const {
        if (!voice_) return 0;
        if (lost_) return lastPlayed_;
        XAUDIO2_VOICE_STATE s{};
        voice_->GetState(&s);
        return lastPlayed_ = s.SamplesPlayed;
    }
    // devolve os blocos que o XAudio2 ja terminou (ficam os BuffersQueued mais recentes)
    void reclaim() {
        XAUDIO2_VOICE_STATE s;
        voice_->GetState(&s, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        while (blocks_.size() > s.BuffersQueued) {
            queuedFrames_ -= blocks_.front().size() / channels_;
            blocks_.pop_front();
        }
    }
    // IXAudio2EngineCallback: perda do dispositivo (fone desligado etc.)
    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT) override { lost_ = true; }

    ComScope                         com_;  // o XAudio2 exige COM na thread da saida
    IXAudio2*                        x_      = nullptr;
    WAVEFORMATEX                     fmt_{};
    uint64_t                         playedBase_ = 0;  // tocados pelas vozes ja descartadas
    IXAudio2MasteringVoice*          master_ = nullptr;
    IXAudio2SourceVoice*             voice_  = nullptr;
    std::deque<std::vector<int16_t>> blocks_;
    uint64_t                         queuedFrames_ = 0;
    uint32_t                         channels_ = 1, rate_ = 44100, latencyMs_;
    bool                             primed_ = false, paused_ = false;
    bool                             follow_ = false;
    static constexpr uint64_t        kCushionMs = 66;  // 4 campos NTSC
    XAudio2Hooks                     hooks_;
    mutable uint64_t                 lastPlayed_ = 0;
    std::wstring                     deviceId_;
    uint64_t                         lastCheck_ = 0;
    Stats                            stats_;
    std::atomic<bool>                lost_{false};
};

} // namespace

std::unique_ptr<IAudioOutput> make_xaudio2_output(uint32_t latency_ms, bool follow_default) {
    return std::make_unique<XAudio2Output>(latency_ms, follow_default);
}
std::unique_ptr<IAudioOutput> make_xaudio2_output(uint32_t latency_ms, bool follow_default, XAudio2Hooks hooks) {
    return std::make_unique<XAudio2Output>(latency_ms, follow_default, std::move(hooks));
}
std::unique_ptr<IAudioOutput> make_null_output() { return std::make_unique<NullOutput>(); }

} // namespace bs::native::audio
