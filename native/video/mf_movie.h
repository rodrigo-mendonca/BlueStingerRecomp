#pragma once
// N5 (docs/N5-PLANO.md, secao 4): filmes por Media Foundation, decisao do mantenedor (2026-10-15).
// MfMovieReader decodifica um arquivo (MP4/H.264 + AAC do Builder, ou o de um mod) em quadros RGB32
// e PCM 16 bits, em ordem, sem relogio proprio: quem toca decide QUAL quadro mostrar pela posicao do
// relogio logico do jogo. MfMovieWriter codifica (Builder e testes sinteticos). So Windows.
#include "video/movie_decoder.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bs::native::video {

// IMovieDecoder pelo Media Foundation (o decodificador do Windows: make_movie_decoder()).
class MfMovieReader final : public IMovieDecoder {
public:
    MfMovieReader();
    ~MfMovieReader();
    MfMovieReader(const MfMovieReader&)            = delete;
    MfMovieReader& operator=(const MfMovieReader&) = delete;

    // Falha com mensagem clara: arquivo ausente, formato sem decodificador, sem video.
    // nv12 = true: quadros em NV12 (o YUV do arquivo, sem conversao) em next_frame_nv12
    bool               open(const std::filesystem::path& file, std::string* err, bool nv12);
    bool               open(const std::filesystem::path& file, std::string* err = nullptr) override { return open(file, err, false); }
    const MovieFormat& format() const override { return fmt_; }
    // Proximo quadro de video; false no fim do stream (ou erro, em `err`).
    bool next_frame(VideoFrame& f, std::string* err = nullptr) override;
    // Com open(..., nv12 = true): plano Y (width*height) e CbCr intercalado, sem alinhamento.
    bool next_frame_nv12(std::vector<uint8_t>& nv12, int64_t& time100ns, std::string* err = nullptr);
    // Proximo bloco de audio (PCM 16 bits intercalado) e o seu tempo; false no fim.
    bool next_audio(std::vector<int16_t>& pcm, int64_t& time100ns, std::string* err = nullptr) override;
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MovieFormat           fmt_;
};

struct WriterOptions {
    uint32_t width = 320, height = 240;
    uint32_t fpsNum = 30, fpsDen = 1;
    uint32_t videoBitrate = 4'000'000;  // H.264, com quality == 0 (VBR sem teto pela media)
    uint32_t quality = 0;  // 1..100: modo qualidade do codificador (ignora o bitrate); 0 = pelo bitrate
    bool     audio = true;
    uint32_t sampleRate = 44100, channels = 2;
    uint32_t audioBytesPerSecond = 24000;  // AAC do Windows: 12000, 16000, 20000 ou 24000 (192 kbit/s, o maximo)
    bool     nv12Input = false;  // quadros em NV12 (o YUV da fonte, sem passar por RGB): write_frame_nv12
};

class MfMovieWriter {
public:
    MfMovieWriter();
    ~MfMovieWriter();
    MfMovieWriter(const MfMovieWriter&)            = delete;
    MfMovieWriter& operator=(const MfMovieWriter&) = delete;

    bool open(const std::filesystem::path& file, const WriterOptions& o, std::string* err = nullptr);
    // Um quadro ARGB (width*height, de cima para baixo), na ordem; o tempo vem do indice.
    bool write_frame(std::span<const uint32_t> argb, std::string* err = nullptr);
    // NV12: plano Y (width*height) seguido de CbCr intercalado (width*height/2), BT.601 faixa de TV.
    bool write_frame_nv12(std::span<const uint8_t> nv12, std::string* err = nullptr);
    // PCM 16 bits intercalado, na ordem; o tempo vem das amostras ja escritas.
    bool write_audio(std::span<const int16_t> pcm, std::string* err = nullptr);
    bool finish(std::string* err = nullptr);
    // O que o codificador aceitou (controle de taxa); vazio se tudo foi aplicado.
    const std::string& notes() const { return notes_; }

private:
    std::string notes_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bs::native::video
