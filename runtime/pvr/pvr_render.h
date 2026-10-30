#pragma once
// Rasterizador de referencia do PowerVR2 (software): executa o render pedido por
// STARTRENDER a partir das listas recebidas pelo TA e grava o resultado no framebuffer
// da VRAM (FB_W_SOF1, formato de FB_W_CTRL). Fiel ao modelo do hardware (buffer de
// tile de 32 bits, profundidade em 1/w, fundo, listas opaca/punch-through/translucida),
// com aproximacoes documentadas em pvr_render.cpp.
#include "cpu/sh4_context.h"
#include "render/kamui/list_sink.h"
#include "render/render.h"
#include "render/soft/soft_raster.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bs {

struct RenderStats {
    uint32_t triangles = 0, sprites = 0, skipped = 0;  // skipped: formatos ainda nao suportados
    uint32_t two_volume = 0;  // N8.7: globais de poligono com "dois volumes" (PCW bit 6), que o jogo nao usa
    uint32_t unsupported = 0;  // N8.7: vertices omitidos de estados que o PVR desenharia (erro no produto)
    uint32_t width = 0, height = 0;
};

RenderStats pvr_render(Sh4Context* c);
// N8.7 4d, so a validacao: a passagem nativa em sombra no rasterizador de referencia ("[render-pass] ...\n"); vazio no
// produto (system/render_bus.cpp)
std::string pvr_render_pass_summary();

// Testes: true se, desde a ultima chamada, algum worker da resolucao paralela calculou com
// MXCSR diferente do padrao (o render deve isolar o ambiente de ponto flutuante).
bool pvr_render_take_fp_violation();

// ---- O que o decodificador das listas do TA produz (N7a: a mesma decodificacao alimenta o rasterizador e a captura
// da IR de render; nenhum segundo parser) ----

// N8.7: os tipos sao os do rasterizador nativo (native/render/soft/soft_raster.h), o mesmo que desenha a IR
using PvrColor  = native::render::soft::Color;
using PvrVertex = native::render::soft::Vertex;
using PvrTri    = native::render::soft::Tri;
using PvrFrame  = native::render::soft::Frame;
PvrFrame pvr_frame_from_regs();
// N8.7: os parametros do quadro da IR (tamanho, fundo, fog, sombra, HALF_OFFSET, clamp...) a partir dos registradores
native::render::FrameParams pvr_frame_params(uint32_t width, uint32_t height);

using TaParam = native::render::kamui::ListParam;  // N8.7: o parametro do TA (pvr/ta.h)

// Captura (N7a): recebe, na ordem de submissao, o que o decodificador produz em cada render. `vertex` e cada vertice
// de um strip com o estado do desenho (os vertices de `state` nao valem; end = fim de strip pelo PCW);
// `strip_break` quando o decodificador recomeca o strip sem fim (global novo, fim de lista); `sprite` os 4 cantos
// (A, B, C e D calculado) de um sprite; `modifier` cada parametro de vertice de volume modificador (listas 1 e 3,
// que o rasterizador ainda nao desenha) com o global dele; `global` cada parametro global (tipos 4 e 5, antes de o
// decodificador aplica-lo: os volumes modificadores fecham pela instrucao do global); `skipped` cada parametro de vertice omitido por estado
// nao suportado. `end` recebe os registradores do quadro usados, os triangulos que o rasterizador desenhou (na ordem
// de submissao) e a imagem ARGB8888 do render (w x h), antes da gravacao no framebuffer.
class PvrCaptureSink {
public:
    virtual ~PvrCaptureSink() = default;
    virtual void begin(uint32_t width, uint32_t height)                                                      = 0;
    virtual void vertex(const PvrTri& state, const PvrVertex& v, bool end_of_strip)                          = 0;
    virtual void strip_break()                                                                               = 0;
    virtual void sprite(const PvrTri& state, const PvrVertex& a, const PvrVertex& b, const PvrVertex& c,
                        const PvrVertex& d)                                                                  = 0;
    virtual void global(const TaParam& p)                                                                    = 0;
    virtual void modifier(const TaParam& global, const TaParam& p)                                           = 0;
    virtual void skipped(const TaParam& p)                                                                   = 0;
    virtual void end(const PvrFrame& frame, std::span<const PvrTri> drawn, std::span<const uint32_t> image, uint32_t w,
                     uint32_t h)                                                                             = 0;
    // N7b: depois da decodificacao, antes do rasterizador em software. true = `image` (w x h ARGB8888, ja com o
    // tamanho certo) foi preenchida por outro renderizador e o software nao roda neste render.
    virtual bool render(const PvrFrame&, uint32_t, uint32_t, std::vector<uint32_t>&) { return false; }
    // N8.7 (produto): a IR que o ListSink decodificou das listas do Kamui; desenha e preenche `image` (w x h ARGB8888).
    // false = nada desenhado (erro explicito no chamador)
    virtual bool render_list(const native::render::kamui::ListFrame&, uint32_t, uint32_t, std::vector<uint32_t>&) { return false; }
};
void pvr_render_set_capture(PvrCaptureSink* sink);  // nullptr desliga
uint32_t pvr_background_color();                     // cor do plano de fundo do render atual (ISP_BACKGND_T), RGB

// Rasteriza triangulos dados (na ordem de submissao) com os registradores de quadro `frame` (nenhum registrador do
// PVR e lido; os texels vem da VRAM atual), sem a VRAM de saida: a imagem ARGB8888 w x h. Mesma regra do pvr_render
// (prova de equivalencia da IR: a imagem a partir da IR tem de ser identica a do caminho do TA).
// N8.7: so no build de validacao (pvr_render_ta.cpp): o rasterizador sobre os triangulos do TA com os texels da VRAM
// N8.7, so na validacao (pvr_render_ta.cpp): o decodificador do legado sobre ta_params(), com os retornos da captura
struct PvrLegacyDecode {
    std::vector<PvrTri>                          prims;
    std::vector<native::render::soft::Volume>    vols;
    std::vector<native::render::soft::VolumeTri> vtris;
};
void   pvr_decode_legacy(PvrCaptureSink* sink, RenderStats& st, PvrLegacyDecode& out);
// as contagens do decodificador do legado no render em andamento (durante PvrCaptureSink::end; nullptr fora dele)
const RenderStats* pvr_render_stats_in_progress();
size_t pvr_render_ta(std::span<const PvrTri> prims, std::span<const native::render::soft::Volume> vols,
                     std::span<const native::render::soft::VolumeTri> vtris, const PvrFrame& fr, native::render::soft::Image& r,
                     native::render::soft::PhaseFn phase, void* ctx);
std::vector<uint32_t> pvr_render_triangles(std::span<const PvrTri> tris, std::span<const native::render::soft::Volume> vols,
                                           std::span<const native::render::soft::VolumeTri> vtris, const PvrFrame& frame,
                                           uint32_t width, uint32_t height);

} // namespace bs
