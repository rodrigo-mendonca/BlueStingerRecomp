#pragma once
// Audio (N6). Motor nativo que reconstroi o que o driver de som (MANATEE.DRV no ARM7)
// fazia: bancos e sequencias do BS_SND.MLT, vozes PCM/ADPCM, volume/pan/pitch, streams.
// A saida e um backend (XAudio2, decisao do mantenedor) atras de IAudioOutput. O ARM7 nunca
// e executado: o driver e documentacao de comportamento (analise estatica).
#include "core/handle.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <span>

namespace bs::native::audio {

struct BankTag;
using BankHandle = Handle<BankTag>;
struct VoiceTag;
using VoiceHandle = Handle<VoiceTag>;
struct StreamTag;
using StreamHandle = Handle<StreamTag>;

struct VoiceParams {
    float volume = 1.0f;  // 0..1
    float pan    = 0.0f;  // -1 (esquerda) .. 1 (direita)
    float pitch  = 1.0f;  // razao de frequencia
    int   priority = 0;
};

enum class StreamCodec : uint8_t { Pcm16, Adx, Adpcm4 };

struct StreamFormat {
    StreamCodec codec = StreamCodec::Pcm16;
    uint32_t    sample_rate = 44100;
    uint8_t     channels = 2;
};

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;
    // Bancos e sequencias: bytes dos arquivos do jogo, interpretados pelo motor nativo.
    virtual Result<BankHandle>  load_bank(std::span<const std::byte> data) = 0;
    virtual void                unload_bank(BankHandle b) = 0;
    virtual Result<VoiceHandle> play(BankHandle b, uint32_t sound, const VoiceParams& p) = 0;
    virtual Result<VoiceHandle> play_sequence(BankHandle b, uint32_t sequence, const VoiceParams& p) = 0;
    virtual void                stop(VoiceHandle v) = 0;
    virtual void                set_params(VoiceHandle v, const VoiceParams& p) = 0;
    virtual bool                playing(VoiceHandle v) const = 0;
    // Streams (musica, audio de FMV): o produtor empurra blocos; o motor mistura.
    virtual Result<StreamHandle> open_stream(const StreamFormat& f) = 0;
    virtual Result<size_t>       push_stream(StreamHandle s, std::span<const std::byte> data) = 0;
    virtual void                 close_stream(StreamHandle s) = 0;
    virtual void                 set_master_volume(float v) = 0;
    // Avanca o motor (sequencias, envelopes) em um quadro logico.
    virtual void update(uint32_t frame) = 0;
};

// Backend de saida: recebe PCM 16 bits intercalado ja mixado.
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;
    virtual Result<void> open(uint32_t sample_rate, uint8_t channels) = 0;
    virtual size_t       writable_frames() const = 0;
    virtual Result<void> write(std::span<const int16_t> interleaved) = 0;
    virtual void         close() = 0;
    // N5 (politica de sincronia, docs/N5-PLANO.md 4.2): pausa da voz (pausa do host), descarte do que
    // esta na fila (salto ou atraso alem do teto) e quadros ja tocados (medida do atraso).
    virtual void     pause(bool paused) = 0;
    virtual void     flush() = 0;
    virtual uint64_t played_frames() const = 0;
    // quadros escritos que ainda nao tocaram (a fila do dispositivo, com a resolucao de um bloco): o atraso entre o que foi
    // misturado e o que se ouve agora. As legendas das falas seguem o audivel (2026-10-23). A saida nula nao tem fila.
    virtual uint64_t queued_frames() const { return 0; }
    // false depois de perder o dispositivo: quem toca troca para a saida nula (com log).
    virtual bool healthy() const = 0;
    // N6-H (tempo real sem estalos): blocos enviados, faltas de dados (um bloco chegou com a fila do dispositivo ja
    // vazia: o dispositivo tocou silencio no meio) e o maior tamanho da fila. A saida nula nao tem faltas.
    // Erros de escrita (a voz recusou o bloco) e perda do dispositivo tambem contam: depois dela nada mais toca.
    struct Stats {
        uint64_t blocks = 0, underruns = 0, maxQueuedFrames = 0, writeErrors = 0;
        uint64_t dropped = 0, reopens = 0;  // modo "seguir a saida padrao": blocos descartados com a fila cheia, reaberturas
        uint64_t cushions = 0;              // idem: reservas de silencio postas com a fila vazia (inicio e depois de faltas)
        bool     lost = false;
    };
    virtual Stats stats() const { return {}; }
};

// Saida XAudio2 (decisao do mantenedor 2026-10-15; no N5 so o audio dos filmes). `latency_ms` e o
// teto da fila: writable_frames() nunca deixa passar disso.
// `follow_default` (audio do jogo, N6): segue a saida padrao do Windows (conferida a cada segundo; trocou ou o dispositivo
// caiu: reabre na saida padrao atual, sem perder a contagem de tocados) e, com a fila do XAudio2 cheia (o jogo atrasou e
// recupera varios campos de uma vez), descarta o bloco em vez de dar o dispositivo por perdido; com a fila vazia (inicio,
// depois de uma falta), poe antes do bloco uma reserva de silencio de ~66 ms (4 campos), que absorve campos atrasados do
// jogo (o adaptador manda um VBlank de audio por campo, sem folga). Nesse modo healthy() fica sempre true (a saida tenta
// de novo a cada segundo). Desligado (filmes, N5): o comportamento de sempre.
std::unique_ptr<IAudioOutput> make_xaudio2_output(uint32_t latency_ms = 200, bool follow_default = false);
// Testes do modo follow_default: a saida padrao, o relogio em ms e uma perda simulada (como o OnCriticalError) injetaveis;
// vazios = os do Windows.
struct XAudio2Hooks {
    std::function<std::wstring()> device_id;
    std::function<uint64_t()>     now_ms;
    std::function<bool()>         force_lost;  // true = o dispositivo caiu agora
};
std::unique_ptr<IAudioOutput> make_xaudio2_output(uint32_t latency_ms, bool follow_default, XAudio2Hooks hooks);
// Saida nula (headless e dispositivo ausente): aceita e descarta, conta os quadros como tocados.
std::unique_ptr<IAudioOutput> make_null_output();

} // namespace bs::native::audio
