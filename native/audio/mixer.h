#pragma once
// N6: mixer nativo do audio de gameplay, portavel (sem Win32: a saida e um IAudioOutput, hoje XAudio2,
// no futuro SDL3 Audio). Tres barramentos (musica, voz, efeitos) com volume proprio; vozes com volume,
// pan, laco por pontos (em quadros), prioridade e roubo de voz; pausa, retomada e parada; eventos de
// conclusao. O tempo e o do chamador: render(n) avanca exatamente n quadros de saida (o relogio logico do
// jogo decide quantos), sem relogio de parede.
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace bs::native::audio {

enum class Bus : uint8_t { Music, Voice, Effects, Count };

// Um som decodificado (PCM 16 bits intercalado). Laco em quadros: [loopStart, loopEnd).
struct SoundData {
    std::vector<int16_t> pcm;
    uint32_t             rate = 44100;
    uint8_t              channels = 1;
    int64_t              loopStart = -1, loopEnd = -1;  // -1: sem laco
    int64_t frames() const { return channels ? int64_t(pcm.size() / channels) : 0; }
};

struct PlayParams {
    Bus      bus      = Bus::Effects;
    float    volume   = 1.0f;  // 0..1
    float    pan      = 0.0f;  // -1 esquerda .. 1 direita
    int      priority = 0;     // maior vence no roubo de voz
    bool     loop     = false; // usa loopStart/loopEnd (sem pontos: o som inteiro)
    uint32_t tag      = 0;     // do chamador (ID do jogo, cue...), devolvido nos eventos
    float    pitch    = 1.0f;  // razao de frequencia (1 = a taxa do som)
    // Fim que o JOGO ve, como POSICAO na fonte (quadros do som; -1: o fim fisico): pausas da voz nao o
    // adiantam. Uma dublagem mais longa que o original avisa LogicalEnd aqui e continua tocando ate o
    // Finished (sound_catalog.h: logical_end_frames na taxa do proprio som).
    int64_t  logicalEnd = -1;
    // Envio ao DSP (fase G: o IMXL/ISEL do canal da AICA): o barramento MIXS (0..15) e o nivel como RAZAO sobre o volume
    // da voz (o envio nao passa pelo DISDL: razao = ganho do IMXL / ganho do DISDL); 0 = sem envio. Assim volume,
    // fades, ducking e soltura valem tambem para o envio. O pan nao vale (o envio e mono).
    float    dspSend = 0.0f;
    uint8_t  dspBus  = 0;
};

class AicaDsp;

using VoiceId = uint32_t;  // 0 = nenhum

enum class EventKind : uint8_t { Finished, Looped, Stopped, Stolen, LogicalEnd };
struct MixerEvent {
    VoiceId   voice;
    uint32_t  tag;
    EventKind kind;
    uint64_t  frame;  // quadro de saida em que aconteceu
};

class AudioMixer {
public:
    // taxa 0 vira 44100 e limite 0 vira 1 (nunca divisao por zero nem voz impossivel)
    explicit AudioMixer(uint32_t outputRate = 44100, uint32_t maxVoices = 32);
    // 0 se recusado (sem voz livre e prioridade menor que a de todas as tocando)
    VoiceId play(std::shared_ptr<const SoundData> s, const PlayParams& p);
    void    stop(VoiceId v);
    // Soltura linear em dB (o envelope da AICA depois do key-off): o ganho cai `dbPerSecond` e a voz acaba (evento
    // Stopped) ao passar de `endDb` (0x3BF unidades de envelope = 89,9 dB). dbPerSecond <= 0 = parar na hora.
    void    release(VoiceId v, double dbPerSecond, double endDb = 89.9);
    void    pause(VoiceId v, bool paused);
    void    pause_all(bool paused);  // pausa do jogo (menus): tudo para, nada se perde
    void    set_volume(VoiceId v, float volume);
    // O volume chega a `volume` em rampa linear de `frames` quadros (envelope por amostra); set_volume cancela a rampa.
    void    set_volume_ramp(VoiceId v, float volume, uint32_t frames);
    void    set_pan(VoiceId v, float pan);
    void    set_pitch(VoiceId v, float pitch);  // a posicao e a fase continuam
    void    set_bus_volume(Bus b, float volume);
    // O DSP de efeitos (fase G): roda uma amostra por quadro de saida (so a 44 100 Hz, a taxa da AICA) sobre os envios das
    // vozes e mistura as 16 saidas com os ganhos esquerda/direita de cada uma (EFSDL e EFPAN). nullptr = sem DSP.
    void    set_dsp(std::shared_ptr<AicaDsp> dsp, const std::array<std::array<float, 2>, 16>& outGains);
    uint64_t dsp_wraps() const { return dspWraps_; }  // amostras de MIXS que passaram de 20 bits (diagnostico)
    // N6-H (H4): amostras em que UMA fonte sozinha (uma voz com o seu ganho, ou uma saida do DSP com o seu ganho) passa
    // de 16 bits. No modelo da AICA a saida de cada canal e de 16 bits (so a SOMA dos canais pode passar e e cortada no
    // fim); fonte acima de 16 bits e clipping introduzido pelo backend (ganho acima de 0 dB, DSP fora da faixa).
    uint64_t source_over() const { return srcOver_; }
    bool    playing(VoiceId v) const;
    float   pitch_of(VoiceId v) const;  // a razao de frequencia atual da voz (0 = nao existe); diagnostico e testes
    size_t  active() const;
    // Mistura `frames` quadros estereo em `out` (2*frames amostras) e avanca o tempo.
    void render(std::span<int16_t> out);
    // Eventos ate o quadro `upTo` (inclusive). A renderizacao vai adiantada (fila da saida, ~200 ms):
    // quem integra passa o quadro ja tocado (IAudioOutput::played_frames) ou o alcancado pelo relogio
    // logico, para o jogo nunca ver um fim antes da hora. Os posteriores ficam para depois.
    std::vector<MixerEvent> take_events(uint64_t upTo = UINT64_MAX);
    uint64_t                now() const { return frame_; }
    uint32_t                rate() const { return rate_; }

private:
    struct Voice {
        VoiceId                          id = 0;
        std::shared_ptr<const SoundData> s;
        PlayParams                       p;
        double                           pos = 0;  // quadro da fonte (fracionario: reamostragem)
        bool                             paused = false;
        uint64_t                         started = 0;
        bool                             logicalSent = false;
        bool                             releasing = false;  // soltura em curso (release)
        float                            rampCur = 0, rampStep = 0;  // rampa de volume (set_volume_ramp)
        uint32_t                         rampLeft = 0;
        float                            relGain = 1.0f, relMul = 1.0f;
        double                           relDb = 0, relStep = 0, relEnd = 0;
    };
    std::shared_ptr<AicaDsp>                dsp_;
    std::array<std::array<float, 2>, 16>    dspOut_{};
    std::vector<float>                      mixs_;  // envios do bloco: 16 por quadro
    uint64_t                                dspWraps_ = 0;
    uint64_t                                srcOver_  = 0;
    void finish(Voice& v, EventKind k);
    uint32_t                rate_, max_;
    std::vector<Voice>      voices_;
    float                   bus_[size_t(Bus::Count)] = {1, 1, 1};
    bool                    allPaused_ = false;
    VoiceId                 next_ = 1;
    uint64_t                frame_ = 0;
    std::vector<MixerEvent> events_;
};

} // namespace bs::native::audio
