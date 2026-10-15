#pragma once
// Audio (N6). Motor nativo que reconstroi o que o driver de som (MANATEE.DRV no ARM7)
// fazia: bancos e sequencias do BS_SND.MLT, vozes PCM/ADPCM, volume/pan/pitch, streams.
// A saida e um backend (XAudio2, decisao do mantenedor) atras de IAudioOutput. O ARM7 nunca
// e executado: o driver e documentacao de comportamento (analise estatica).
#include "core/handle.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
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
};

} // namespace bs::native::audio
