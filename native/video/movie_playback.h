#pragma once
// N5 (docs/N5-PLANO.md, secao 4.2): um filme tocado pelo relogio LOGICO do jogo. Quem chama (o slot
// da funcao de video) informa a posicao uma vez por iteracao, depois da espera do guest; o filme
// mostra o ultimo quadro com tempo <= posicao (pula quadros se o relogio correr, repete se parar) e
// entrega o audio decodificado ate a posicao mais a antecedencia pedida. Sem relogio de parede: o
// mesmo resultado no headless e na janela, nos dois modos de tempo.
#include "video/mf_movie.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace bs::native::video {

// Quando o filme "acaba" para o jogo (o original: estado 3/4 do Sofdec; fixado pelo oraculo O1).
enum class EndRule : uint8_t {
    VideoEnd,     // fim do ultimo quadro de video
    AudioEnd,     // fim do audio (sem audio: o video)
    LaterOfBoth,  // o que acabar por ultimo
};

struct PlaybackOptions {
    EndRule end = EndRule::LaterOfBoth;
    int64_t audioLead100ns = 2'000'000;  // antecedencia do audio entregue (200 ms)
    // Recurso do Builder (manifesto): amostras de priming do AAC descartadas do inicio do audio (por
    // canal) e os fins de cada stream NA FONTE original, que valem para a regra de fim no lugar dos
    // EOFs do MP4 (o AAC acrescenta preenchimento). Zero = os do proprio arquivo (mods).
    int64_t audioSkipFrames = 0;
    int64_t sourceVideoEnd100ns = 0, sourceAudioEnd100ns = 0;
};

class MoviePlayback {
public:
    bool open(const std::filesystem::path& file, const PlaybackOptions& o = {}, std::string* err = nullptr);
    const MovieFormat& format() const { return reader_.format(); }

    // Avanca ate `position100ns` (tempo do filme). Devolve true se o quadro atual mudou.
    bool advance(int64_t position100ns, std::string* err = nullptr);
    const VideoFrame& frame() const { return current_; }
    bool              has_frame() const { return hasCurrent_; }
    uint64_t          frames_shown() const { return shown_; }    // quadros distintos exibidos
    uint64_t          frames_skipped() const { return skipped_; }  // decodificados e nao exibidos

    // Audio PCM ate posicao + antecedencia, em ordem (consome o que devolve).
    std::vector<int16_t> take_audio(int64_t position100ns, std::string* err = nullptr);

    // Pela regra de fim, depois de advance() e take_audio() na mesma posicao (o fim do audio so e
    // conhecido consumindo-o; sem saida de audio, quem chama descarta o que take_audio devolve).
    bool    finished(int64_t position100ns) const;
    int64_t video_end100ns() const { return videoEnd_; }  // conhecido no fim do stream
    int64_t audio_end100ns() const { return audioEnd_; }

private:
    MfMovieReader    reader_;
    PlaybackOptions  o_;
    VideoFrame       current_, next_;
    bool             hasCurrent_ = false, hasNext_ = false, videoEof_ = false, audioEof_ = false;
    int64_t          videoEnd_ = -1, audioEnd_ = -1;
    uint64_t         shown_ = 0, skipped_ = 0;
    std::deque<int16_t> audio_;
    int64_t             audioQueuedUntil_ = 0;  // tempo do fim do audio ja decodificado
    int64_t             skipLeft_ = 0;          // amostras de priming ainda a descartar (intercaladas)
};

} // namespace bs::native::video
