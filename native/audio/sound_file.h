#pragma once
// N6: leitura de um arquivo de som para SoundData (PCM 16 bits), portavel.
//   WAV PCM 8/16 bits (mods) e WAV ADPCM da AICA (format tag 0, o formato original dos eventos).
//   Laco: chunk "smpl" do WAV (primeiro laco), se houver.
//   FLAC: dr_flac (third_party/dr_libs, so em sound_flac.cpp), para dublagens e mods (N6 fase E).
//   OGG/Vorbis: libogg 1.3.6 + libvorbis 1.3.7 (Xiph, third_party/xiph, so em sound_vorbis.cpp), decisao do
//   mantenedor de 2026-10-20 (o stb_vorbis tem CVEs de 2023 sem correcao publicada).
// Arquivos de mods sao entrada nao confiavel: tamanho e duracao limitados antes de decodificar.
#include "audio/mixer.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace bs::native::audio {

std::shared_ptr<SoundData> load_sound(const std::filesystem::path& file, std::string* err = nullptr);
std::shared_ptr<SoundData> parse_wav(std::span<const uint8_t> bytes, std::string* err = nullptr);
std::shared_ptr<SoundData> parse_flac(std::span<const uint8_t> bytes, std::string* err = nullptr);
std::shared_ptr<SoundData> parse_ogg(std::span<const uint8_t> bytes, std::string* err = nullptr);  // OGG/Vorbis

inline constexpr size_t   kMaxSoundFileBytes = 256u << 20;  // 256 MB
inline constexpr uint32_t kMaxSoundSeconds   = 30 * 60;     // 30 min (a fala original mais longa tem 53 s)
// PCM decodificado (amostras de 16 bits, todos os canais): 128 MB. O limite do arquivo nao limita o PCM: um FLAC pequeno
// e muito compressivel pode declarar 30 min de estereo a 192 kHz (revisao Codex 2026-10-20)
inline constexpr uint64_t kMaxSoundPcmSamples = 64ull << 20;

} // namespace bs::native::audio
