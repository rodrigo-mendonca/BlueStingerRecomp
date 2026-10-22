#pragma once
// N7c: legendas das falas sobre o GAMEPLAY. O texto vem do SubtitleService do N6 (eventos de fala no tempo logico,
// publicado com o quadro do mesmo VBlank: GameFrame::subtitle); aqui so o desenho, numa camada propria por cima da
// imagem do jogo, que nao e tocada (vale igual para o renderizador em software e para o Vulkan). O rasterizador e o
// das legendas dos filmes (native/video, DirectWrite), em modo de camada transparente.
//   - na janela: a camada na resolucao da janela, misturada por cima do quadro (texto nitido em qualquer escala);
//   - nas capturas (--capture, F12): composta sobre uma copia do quadro, para o teste ver o que o jogador ve.
#include "game_host.h"
#include "video/subtitles.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bs {

class SubtitleOverlay {
public:
    explicit SubtitleOverlay(const native::video::OverlayStyle& style = {});
    bool ok() const;
    void set_style(const native::video::OverlayStyle& style);

    // A camada (ARGB pre-multiplicado, width x height) para `text` com a area do jogo (cx, cy, cw, ch); refeita so
    // quando o texto, o tamanho ou o estilo mudam. nullptr = sem legenda (texto vazio).
    const std::vector<uint32_t>* layer(const std::wstring& text, uint32_t width, uint32_t height, float cx, float cy,
                                       float cw, float ch);

    // O quadro com a legenda dele por cima (as capturas mostram o que o jogador ve). Sem legenda: o proprio quadro.
    GameFrame composed(const GameFrame& f);
    uint64_t  version() const { return version_; }  // muda a cada camada redesenhada (a apresentacao reenvia so entao)

private:
    std::unique_ptr<native::video::ISubtitleRasterizer> raster_;
    native::video::OverlayStyle                         style_;
    std::vector<uint32_t>                               buf_;
    std::wstring                                        text_;
    uint32_t                                            w_ = 0, h_ = 0;
    float                                               area_[4] = {};
    bool                                                valid_ = false;
    uint64_t                                            version_ = 0;
};

// mistura pre-multiplicada: dst = src + dst * (1 - alfa(src)), por canal
void blend_premultiplied(std::vector<uint32_t>& dst, const std::vector<uint32_t>& src);

} // namespace bs
