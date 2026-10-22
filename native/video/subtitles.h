#pragma once
// N5 (D3): legendas externas dos filmes (SRT e WebVTT, UTF-8), opcionais, por idioma e substituiveis
// por mods (native/video/movie_catalog.h). O jogo original nao tem legendas. O desenho e por
// DirectWrite sobre o quadro ARGB, proporcional a altura do video (vale para 320x240 e para um mod 4K).
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::video {

struct SubtitleCue {
    int64_t      start100ns = 0, end100ns = 0;
    std::wstring text;  // linhas separadas por '\n', sem tags (<i>, <b>, {\an8}...)
};

// SRT ou WebVTT (pelo conteudo: "WEBVTT" no inicio). Cues em ordem de inicio. Linhas ou tempos
// invalidos sao ignorados com um aviso em `warnings`; arquivo ilegivel devolve false.
bool load_subtitles(const std::filesystem::path& file, std::vector<SubtitleCue>& cues, std::string* err = nullptr,
                    std::vector<std::string>* warnings = nullptr);
bool parse_subtitles(std::string_view utf8, std::vector<SubtitleCue>& cues, std::vector<std::string>* warnings = nullptr);
// Texto ativo no instante (cues sobrepostos: juntados em ordem); vazio se nenhum.
std::wstring subtitle_at(const std::vector<SubtitleCue>& cues, int64_t t100ns);

// N7c: estilo das legendas sobre o gameplay (camada propria, por cima da imagem do jogo)
struct OverlayStyle {
    float heightFraction = 0.045f;  // altura do texto / altura da area do jogo (escala com a resolucao)
    float scale          = 1.0f;    // opcao do jogador (tamanho do texto)
    bool  outline        = true;    // contorno escuro (legibilidade sem fundo)
    bool  box            = false;   // fundo escuro translucido atras do texto
    float boxAlpha       = 0.6f;
    float safeMargin     = 0.06f;   // fracao da area do jogo nas bordas
};

// Rasterizacao do texto da legenda sobre o quadro ARGB (o parser acima e portavel).
class ISubtitleRasterizer {
public:
    virtual ~ISubtitleRasterizer() = default;
    virtual bool ok() const = 0;  // backend disponivel
    // Desenha `text` centralizado embaixo, branco com contorno escuro, sobre o quadro.
    virtual void draw(std::span<uint32_t> argb, uint32_t width, uint32_t height, const std::wstring& text) = 0;
    // N7c (gameplay): so o texto, numa camada TRANSPARENTE w x h (ARGB pre-multiplicado, limpa aqui), centralizado
    // embaixo dentro da area do jogo (cx, cy, cw, ch: a imagem 4:3 numa tela de outra proporcao). Quem apresenta a
    // mistura por cima do quadro; a imagem do jogo nao e tocada.
    virtual void draw_overlay(std::span<uint32_t> premultiplied, uint32_t width, uint32_t height, const std::wstring& text,
                              float cx, float cy, float cw, float ch, const OverlayStyle& style) = 0;
};
// O rasterizador da plataforma (Windows: SubtitleRenderer, DirectWrite/Direct2D).
std::unique_ptr<ISubtitleRasterizer> make_subtitle_rasterizer();

class SubtitleRenderer final : public ISubtitleRasterizer {
public:
    SubtitleRenderer();
    ~SubtitleRenderer();
    SubtitleRenderer(const SubtitleRenderer&)            = delete;
    SubtitleRenderer& operator=(const SubtitleRenderer&) = delete;
    bool ok() const override;  // DirectWrite/Direct2D disponiveis
    void draw(std::span<uint32_t> argb, uint32_t width, uint32_t height, const std::wstring& text) override;
    void draw_overlay(std::span<uint32_t> premultiplied, uint32_t width, uint32_t height, const std::wstring& text, float cx,
                      float cy, float cw, float ch, const OverlayStyle& style) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bs::native::video
