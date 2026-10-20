// N6 fase E: FLAC das dublagens e do audio de mods (third_party/dr_libs/README.md). O unico arquivo que inclui o
// decodificador: o resto do audio so ve parse_flac (sound_file.h), e trocar a biblioteca nao muda a interface.
// Entrada nao confiavel: so da memoria, limites conferidos antes de decodificar, sem callback de metadados.
#include "audio/sound_file.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG  // FLAC dentro de Ogg nao e aceito (um formato por extensao)
#if defined(_MSC_VER)
#pragma warning(push, 0)  // avisos do cabecalho de terceiros (sem alteracoes locais)
#endif
#include "dr_flac.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <memory>
#include <new>
#include <string>

namespace bs::native::audio {

std::shared_ptr<SoundData> parse_flac(std::span<const uint8_t> bytes, std::string* err) {
    auto fail = [&](const std::string& m) -> std::shared_ptr<SoundData> {
        if (err) *err = m;
        return nullptr;
    };
    if (bytes.size() > kMaxSoundFileBytes) return fail("FLAC grande demais");
    drflac* f = drflac_open_memory(bytes.data(), bytes.size(), nullptr);
    if (!f) return fail("FLAC invalido");
    std::unique_ptr<drflac, decltype(&drflac_close)> guard(f, &drflac_close);
    if (f->channels < 1 || f->channels > 2) return fail("FLAC com " + std::to_string(f->channels) + " canais (so mono ou estereo)");
    if (f->sampleRate < 8000 || f->sampleRate > 192000) return fail("FLAC com taxa fora de 8000..192000 Hz");
    const uint64_t frames = f->totalPCMFrameCount;
    if (frames == 0) return fail("FLAC sem duracao conhecida ou vazio");
    if (frames > uint64_t(kMaxSoundSeconds) * f->sampleRate) return fail("FLAC longo demais");
    if (frames > kMaxSoundPcmSamples / f->channels) return fail("FLAC grande demais depois de decodificado");
    auto s      = std::make_shared<SoundData>();
    s->rate     = f->sampleRate;
    s->channels = f->channels;
    try {
        s->pcm.resize(size_t(frames) * f->channels);
    } catch (const std::bad_alloc&) {
        return fail("FLAC: memoria insuficiente");
    }
    const uint64_t got = drflac_read_pcm_frames_s16(f, frames, s->pcm.data());
    if (got == 0) return fail("FLAC sem amostras decodificaveis");
    s->pcm.resize(size_t(got) * f->channels);  // arquivo truncado: o que decodificou
    return s;
}

} // namespace bs::native::audio
