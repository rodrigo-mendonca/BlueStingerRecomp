#pragma once
// N7a: captura da IR de render (native/render/render.h) a partir do que o decodificador das listas do TA produz
// (runtime/pvr/pvr_render.h, PvrCaptureSink): o rasterizador em software continua igual e e a referencia. Cada render
// vira uma CommandList (strips como Draw, sprites como dois triangulos (A, B, C) e (A, C, D), o estado em DrawState,
// o quadro em FrameParams, os volumes modificadores fechados pela instrucao do global) com a tabela das texturas
// usadas (TextureHandle = indice + 1).
//
// Validacao (BS_RENDER_IR=conferir; BS_RENDER_IR_CADA=N confere 1 render a cada N, padrao 1):
//   - validate() da IR;
//   - os triangulos que a IR descreve (strips com a alternancia de sentido do PVR) sao os que o rasterizador
//     desenhou: mesmos vertices e o mesmo estado efetivo, na mesma ordem;
//   - os FrameParams reproduzem os registradores de quadro que o rasterizador usou (pvr_frame);
//   - a imagem redesenhada so a partir da IR (triangulos e FrameParams; pvr_render_triangles nao le registradores)
//     e IDENTICA a do caminho do TA;
//   - cada textura usada, convertida pelas conversoes nativas (native/render/texture_convert) a partir dos bytes da
//     VRAM, e igual texel a texel ao decodificador do legado (pvr_texel).
// BS_RENDER_IR=capturar so monta a IR (para o backend). Resumo no fim: render_capture::summary().
#include "pvr/pvr_render.h"
#include "render/render.h"
#include "render/texture_replacement.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bs::transitional::render_capture {

namespace nr = bs::native::render;

// O que identifica os texels de uma textura (a IR so guarda o handle): TCW (endereco, formato, paleta, modos),
// tamanho do TSP e o stride linear (TEXT_CONTROL).
struct TextureKey {
    uint32_t tcw = 0, width = 0, height = 0, stride = 0;
    bool     operator==(const TextureKey&) const = default;
};

struct Frame {
    nr::CommandList         cl;
    std::vector<TextureKey> textures;  // TextureHandle.value - 1
    uint32_t                texture_stride = 0;  // TEXT_CONTROL do render (stride das texturas lineares)
    uint32_t                modifier_params = 0, modifier_volumes = 0, modifier_unclosed = 0, skipped_params = 0,
                            sprites = 0;
};

// IR estruturalmente valida (validate) e handles de textura dentro da tabela do quadro.
bool valid(const Frame& f, std::string* error);

// Conversoes (as regras ficam aqui, num lugar so): o estado decodificado do PVR -> DrawState, e a volta, que prova
// que a IR guarda tudo o que o rasterizador usa (a imagem a partir da IR tem de ser identica).
nr::DrawState  draw_state(const PvrTri& t, uint32_t texture_handle);
PvrTri         pvr_state(const nr::DrawState& s, const TextureKey* tex);  // sem os vertices
nr::Vertex     vertex(const PvrVertex& v);
PvrVertex      pvr_vertex(const nr::Vertex& v);
nr::FrameParams frame_params(uint32_t width, uint32_t height);  // dos registradores atuais do PVR
PvrFrame       pvr_frame(const Frame& f);  // a volta: o que o rasterizador usa, so a partir da IR

// Os triangulos que a IR descreve, na ordem de submissao (strip: o k-esimo com os vertices k, k+1, k+2, os dois
// primeiros trocados em k impar, como o decodificador do TA).
std::vector<PvrTri> triangles(const Frame& f);

// Texels de uma textura (ARGB8888 linear, o maior nivel) pelas conversoes nativas; false se o formato nao tem
// conversao nativa (paleta de 8 bits, YUV, bump: o jogo nao os usa nos arquivos, docs/N7-PLANO.md secao 12).
bool convert_texture(const TextureKey& k, std::vector<uint32_t>& argb);

// Identidade estavel por conteudo (native/render/texture_replacement.h) da textura que `k` descreve agora na VRAM:
// a chave futura das substituicoes HD. false se o formato nao tem conversao nativa.
bool content_id(const TextureKey& k, nr::TextureId& id);

struct Stats {
    uint64_t renders = 0, checked = 0, draws = 0, vertices = 0, triangles = 0, sprites = 0;
    uint64_t invalid = 0, triangle_mismatch = 0, image_mismatch = 0;  // tem de ficar em 0
    uint64_t textures_checked = 0, texture_mismatch = 0, textures_without_native = 0;
    uint64_t modifier_params = 0, modifier_volumes = 0, modifier_unclosed = 0, skipped_params = 0;
    uint64_t frame_mismatch = 0;  // FrameParams que nao reproduzem os registradores usados (tem de ficar em 0)
    uint64_t distinct_texture_keys = 0, distinct_texture_ids = 0;  // chaves (TCW+tamanho) e ids de conteudo distintos
    std::string first_error;
};

enum class Mode : uint8_t { Off, Capture, Check };
void         install(Mode m, uint32_t every = 1);  // instala no pvr_render e zera as estatisticas (Off desinstala)
const Frame& last_frame();                         // a IR do ultimo render
const Stats& stats();
std::string  summary();

} // namespace bs::transitional::render_capture
