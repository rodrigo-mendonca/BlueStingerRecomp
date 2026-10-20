#pragma once
// N5 (docs/N5-PLANO.md, secao 3.1): nucleo do provedor nativo das entradas MW (Sofdec) que a funcao de
// video do jogo (8C08B544) chama. A funcao do jogo continua a original; aqui so o que o player MW
// fazia: abrir o filme (pelo catalogo, com mods), avancar pelo relogio LOGICO, informar o estado,
// guardar a cor base/offset do sprite e entregar o quadro e o audio. Sem Sh4Context (a cola com o
// guest fica em video_adapter.cpp): testavel sozinho, sem GPU.
//
// Confirmado (oraculo O1, jogo original no Flycast, analysis/n5/o1/o1a.tsv):
//   - estados devolvidos por mwSfdGetStat: 1 logo depois do start, 2 tocando, 3 fim natural (E001,
//     ID 3), 4 no NEWGAME (ID 1; ver PENDENTE abaixo);
//   - o stop e chamado depois do estado 3/4 (e o jogo tambem chama stop antes do start).
// PENDENTE (O1b; parametrizado, sem regra inventada):
//   - quantas consultas ficam em 1 (preparando) antes do 2 (O1: ~7 passos do oraculo);
//   - o instante do estado 3 (fim do video, do audio ou o mais tardio): EndRule;
//   - o estado para filme ausente (hipotese: 4, erro de abertura do Sofdec).
// Cor do sprite (video/movie_color.h): modelo parametrizado em MwCoreOptions::color, identidade por
// padrao ate a evidencia do O1b ser confirmada (analysis/n5/o1b, tools/dev/n5_cor.py). A faixa de TV
// so vale para os filmes do Builder (o decodificador original); um mod fica na faixa cheia.
#include "audio/audio.h"
#include "video/movie_catalog.h"
#include "video/movie_color.h"
#include "video/movie_playback.h"
#include "video/subtitles.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace bs::transitional::video {

enum MwStat : int32_t { kMwStop = 0, kMwPrep = 1, kMwPlaying = 2, kMwPlayEnd = 3, kMwError = 4 };

struct MwCoreOptions {
    native::video::CatalogOptions catalog;
    int                           prepPolls = 1;  // PENDENTE (O1b)
    native::video::EndRule        endRule = native::video::EndRule::LaterOfBoth;  // PENDENTE (O1b)
    int32_t                       missingStat = kMwError;  // PENDENTE (O1b): hipotese
    std::string                   subtitleLang;  // "" = sem legendas; senao o idioma (pt, en...)
    native::video::MovieColorModel color;  // cor base/offset do sprite (identidade ate o O1b confirmar)
    std::function<std::unique_ptr<native::audio::IAudioOutput>()> makeAudio;  // vazio = saida nula
    std::function<void(const std::string&)>                       log;
};

struct MwCoreStats {
    uint64_t starts = 0, stops = 0, framesShown = 0, framesSkipped = 0, audioFlushes = 0, statusCalls = 0;
    bool     audioLost = false;
};

class MwPlayerCore {
public:
    explicit MwPlayerCore(MwCoreOptions o);
    ~MwPlayerCore();

    // start(nome do arquivo como o jogo passa, com ou sem pasta). Devolve o estado inicial.
    int32_t start(std::string_view fname, int64_t now100ns);
    // Uma consulta do jogo (uma por iteracao do laco do filme): avanca ate `now`, entrega audio.
    int32_t status(int64_t now100ns);
    void    stop();
    void    pause(bool paused);  // pausa do host (o relogio logico tambem para)
    void    set_base(uint32_t v) { base_ = v, colorDirty_ = true; }
    void    set_offset(uint32_t v) { offset_ = v, colorDirty_ = true; }
    uint32_t base() const { return base_; }
    uint32_t offset() const { return offset_; }

    // Quadro atual com as legendas (para o apresentador do host); false sem quadro.
    bool                                  frame(native::video::VideoFrame& out);
    const native::video::MovieEntry*      entry() const { return entry_; }
    const MwCoreStats&                    stats() const { return stats_; }
    int32_t                               state() const { return state_; }
    const native::video::MovieCatalog&    catalog() const { return catalog_; }

private:
    void feedAudio(int64_t pos);

    MwCoreOptions                                 o_;
    native::video::MovieCatalog                   catalog_;
    const native::video::MovieEntry*              entry_ = nullptr;
    std::unique_ptr<native::video::MoviePlayback> play_;
    std::unique_ptr<native::audio::IAudioOutput>  audio_;
    std::vector<native::video::SubtitleCue>       subs_;
    std::vector<int16_t>                          pending_;  // audio decodificado que ainda nao coube na fila
    std::unique_ptr<native::video::ISubtitleRasterizer> subRenderer_;
    int64_t                                       start100ns_ = 0, lastPos_ = 0;
    int32_t                                       state_ = kMwStop;
    int                                           prepLeft_ = 0;
    uint32_t                                      base_ = 0xFF, offset_ = 0;
    native::video::MovieColor                     color_;
    bool                                          colorDirty_ = true;
    MwCoreStats                                   stats_;
};

} // namespace bs::transitional::video
