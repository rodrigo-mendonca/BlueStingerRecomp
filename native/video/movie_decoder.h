#pragma once
// N5: contrato do decodificador de filmes, independente de plataforma (docs/DECISAO-VULKAN-LINUX.md,
// secao 3). Decodifica um arquivo em quadros ARGB e PCM 16 bits, em ordem, sem relogio proprio: quem
// toca (MoviePlayback) decide QUAL quadro mostrar pela posicao do relogio logico do jogo. O quadro fica
// em memoria (ARGB), sem textura ou superficie de API grafica: o renderer (hoje o host, no N7 o Vulkan)
// o recebe como esta.
//   Windows: Media Foundation (video/mf_movie.h). Outra plataforma: outra implementacao desta interface.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bs::native::video {

struct MovieFormat {
    uint32_t width = 0, height = 0;
    double   fps = 0;
    int64_t  duration100ns = 0;  // 0 se o arquivo nao informa
    bool     hasAudio = false;
    uint32_t sampleRate = 0, channels = 0;
};

struct VideoFrame {
    std::vector<uint32_t> argb;  // width * height, de cima para baixo, alfa 0xFF
    uint32_t              width = 0, height = 0;
    int64_t               time100ns = 0, duration100ns = 0;
};

class IMovieDecoder {
public:
    virtual ~IMovieDecoder() = default;
    // Falha com mensagem clara: arquivo ausente, formato sem decodificador, sem video.
    virtual bool               open(const std::filesystem::path& file, std::string* err = nullptr) = 0;
    virtual const MovieFormat& format() const = 0;
    // Proximo quadro de video; false no fim do stream (ou erro, em `err`).
    virtual bool next_frame(VideoFrame& f, std::string* err = nullptr) = 0;
    // Proximo bloco de audio (PCM 16 bits intercalado) e o seu tempo; false no fim.
    virtual bool next_audio(std::vector<int16_t>& pcm, int64_t& time100ns, std::string* err = nullptr) = 0;
    virtual void close() = 0;
};

// O decodificador da plataforma (Windows: Media Foundation).
std::unique_ptr<IMovieDecoder> make_movie_decoder();

} // namespace bs::native::video
