#pragma once
// Apresentacao das legendas (docs/LOCALIZACAO-PLANO.md, secao 9): o SubtitleService decide o texto; quem desenha
// implementa ISubtitlePresenter (N7: Vulkan sobre o filme e sobre o gameplay; hoje so o rasterizador do N5 dentro
// do quadro do filme). O layout e portavel e testavel: quebra de linha, escala pela altura, margens seguras, area
// do conteudo 4:3 numa tela de outra proporcao e retangulos a evitar (HUD). A medida do texto vem de quem desenha
// (ITextMeasurer: fonte externa, Unicode, japones). Sem Win32, DirectWrite ou Vulkan aqui.
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::localization {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool  intersects(const Rect& o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
};

struct SubtitleStyle {
    std::filesystem::path font;              // fonte do mod (vazio: a padrao do apresentador)
    float                 heightFraction = 0.045f;  // altura do texto / altura do conteudo (escala com a resolucao)
    float                 scale          = 1.0f;    // opcao do jogador
    uint32_t              color = 0xFFFFFFFF, outline = 0xFF000000, shadow = 0x80000000;  // ARGB
    float                 outlinePx = 2.0f, shadowPx = 2.0f;
    float                 safeMargin = 0.05f;       // fracao do conteudo nas bordas
    int                   maxLines   = 3;
};

class ITextMeasurer {
public:
    virtual ~ITextMeasurer() = default;
    virtual float width(std::u32string_view text, float px) = 0;  // largura de uma linha
    virtual float line_height(float px) = 0;
};

struct SubtitleLayout {
    std::vector<std::u32string> lines;
    Rect                        box;   // onde o bloco fica (pixels da tela)
    float                       px = 0;
    bool                        truncated = false;  // passou de maxLines (o resto e cortado com "...")
};

// Bloco embaixo e centralizado na area do conteudo; sobe acima de qualquer retangulo de `avoid` que cubra.
// `content`: a area do jogo na tela (4:3 com barras numa tela 16:9). Quebra por espacos; palavra maior que a
// linha (japones sem espacos) quebra por caractere.
SubtitleLayout layout_subtitle(std::u32string_view text, const Rect& content, const SubtitleStyle& style,
                               std::span<const Rect> avoid, ITextMeasurer& measure);

std::u32string utf8_to_u32(std::string_view s);

class ISubtitlePresenter {
public:
    virtual ~ISubtitlePresenter() = default;
    // Um quadro: os blocos ativos (do SubtitleService, ja com layout) sobre o filme ou sobre o gameplay.
    virtual void present(std::span<const SubtitleLayout> blocks, const SubtitleStyle& style) = 0;
};

} // namespace bs::native::localization
