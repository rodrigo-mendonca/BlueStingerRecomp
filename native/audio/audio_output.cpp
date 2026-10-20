// N5 (D2): saidas de audio atras de IAudioOutput. XAudio2 com uma voz de origem PCM 16 bits e uma fila
// de blocos proprios (o XAudio2 le da memoria de quem enviou ate o fim do bloco); nula para o headless.
#include "audio/audio.h"
#include "core/com_scope.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

namespace bs::native::audio {

namespace {

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
    explicit XAudio2Output(uint32_t latencyMs) : latencyMs_(latencyMs) {}
    ~XAudio2Output() override { close(); }

    Result<void> open(uint32_t rate, uint8_t ch) override {
        close();
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
        voice_->Start();
        return {};
    }

    size_t writable_frames() const override {
        if (!voice_ || lost_) return 0;
        const_cast<XAudio2Output*>(this)->reclaim();
        const uint64_t cap = uint64_t(rate_) * latencyMs_ / 1000;
        return queuedFrames_ >= cap ? 0 : size_t(cap - queuedFrames_);
    }

    Result<void> write(std::span<const int16_t> pcm) override {
        if (!voice_ || lost_) return Error::Io;
        if (pcm.empty()) return {};
        reclaim();
        blocks_.emplace_back(pcm.begin(), pcm.end());
        XAUDIO2_BUFFER b{};
        b.AudioBytes = UINT32(blocks_.back().size() * 2);
        b.pAudioData = reinterpret_cast<const BYTE*>(blocks_.back().data());
        if (FAILED(voice_->SubmitSourceBuffer(&b))) {  // a voz nao aceita mais: tratado como dispositivo perdido
            blocks_.pop_back();
            lost_ = true;
            return Error::Io;
        }
        queuedFrames_ += pcm.size() / channels_;
        return {};
    }

    void close() override {
        if (voice_) voice_->DestroyVoice(), voice_ = nullptr;
        if (master_) master_->DestroyVoice(), master_ = nullptr;
        if (x_) x_->Release(), x_ = nullptr;
        blocks_.clear();
        queuedFrames_ = 0;
    }

    void pause(bool p) override {
        if (!voice_) return;
        p ? voice_->Stop() : voice_->Start();
    }

    // Descarte da fila: a voz e destruida (DestroyVoice e sincrono: depois dele o XAudio2 nao le mais
    // nenhum bloco) e recriada; so entao os blocos saem. O FlushSourceBuffers nao garante isso a tempo
    // (revisao Codex 2026-10-19).
    void flush() override {
        if (!voice_) return;
        playedBase_ += currentPlayed();
        voice_->DestroyVoice();
        voice_ = nullptr;
        blocks_.clear();
        queuedFrames_ = 0;
        if (FAILED(x_->CreateSourceVoice(&voice_, &fmt_))) {
            voice_ = nullptr;
            lost_  = true;
            return;
        }
        voice_->Start();
    }

    uint64_t played_frames() const override { return playedBase_ + currentPlayed(); }

    bool healthy() const override { return voice_ && !lost_; }

private:
    uint64_t currentPlayed() const {
        if (!voice_) return 0;
        XAUDIO2_VOICE_STATE s;
        voice_->GetState(&s);
        return s.SamplesPlayed;
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
    std::atomic<bool>                lost_{false};
};

} // namespace

std::unique_ptr<IAudioOutput> make_xaudio2_output(uint32_t latency_ms) { return std::make_unique<XAudio2Output>(latency_ms); }
std::unique_ptr<IAudioOutput> make_null_output() { return std::make_unique<NullOutput>(); }

} // namespace bs::native::audio
