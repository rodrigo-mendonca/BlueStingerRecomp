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
//
// N7b (backend Vulkan, native/render/vulkan): Mode::Native (--render nativo) desenha a IR no Vulkan e a imagem
// substitui a do software (o resto do caminho, gravacao no framebuffer e apresentacao, nao muda); sem Vulkan,
// fica no software com o motivo no resumo. Mode::Compare (BS_RENDER_IR=comparar) mantem o software como saida e
// compara a imagem do Vulkan pixel a pixel (BS_RENDER_IR_BMP=<pasta>: software | Vulkan | diferenca x8).
// Texturas: as conversoes nativas; sem conversao nativa (paleta de 8 bits, YUV, bump), o decodificador do legado
// texel a texel (contadas, nunca descartadas). O cache do backend refaz uma textura quando os bytes dela na VRAM
// (ou a paleta) mudam.
//
// N8.7 5a (docs/N8-7-KAMUI.md): duas partes. O renderizador sobre a IR (render_core: dispositivo, texturas, estatisticas,
// esta API) entra nos dois builds; no produto ele recebe a IR do ListSink (PvrListRenderer). O adaptador do decodificador
// do legado (render_capture.cpp: PvrCaptureSink, modo conferir, ListSink contra a captura) so na validacao.
#include "pvr/pvr_render.h"
#include "render/render.h"
#include "render/texture_replacement.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bs::transitional::render_capture {

namespace nr = bs::native::render;

// O que identifica os texels de uma textura (a IR so guarda o handle): TCW (endereco, formato, paleta, modos),
// tamanho do TSP e o stride linear (TEXT_CONTROL).
// N8.7: o mesmo tipo do ListSink nativo (native/render/kamui/list_sink.h)
using TextureKey = bs::native::render::kamui::TextureKey;

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
    // Cobertura de fidelidade: quantos desenhos realmente pediram cada modo de fog e em qual render apareceram
    // primeiro; o primeiro render com volumes permite extrair uma cena real para o oraculo do Flycast.
    std::array<uint64_t, 4> fog_draws{}, fog_first_render{};
    uint64_t modifier_first_render = 0;
    // N8.7 (contrato da IR): desenhos com o bit de sombra (PCW bit 7) e o primeiro render deles; volumes da lista 3 (sobre
    // os translucidos); globais de "dois volumes" (PCW bit 6, sem suporte: tem de ficar em 0). Os parametros da fog e a
    // escala de sombra no primeiro render que usa cada um (dimensionam o efeito visivel).
    uint64_t shadow_draws = 0, shadow_first_render = 0, translucent_volumes = 0, two_volume = 0;
    uint64_t volume_inclusion = 0, volume_exclusion = 0, volume_or = 0, volume_mixed_cull = 0, volume_bad_instruction = 0;
    uint32_t fog_density_first = 0, fog_color_first = 0, fog_table_nonzero_first = 0, fog_table_max_first = 0;
    uint32_t shadow_scale_first = 0;
    uint64_t frame_mismatch = 0;
    // N8.7: o renderizador em software sobre a IR (quadros desenhados, falhas) e, no modo conferir, as imagens dele que
    // diferem do caminho do TA (tem de ficar em 0)
    uint64_t    soft_frames = 0, soft_failures = 0, soft_image_mismatch = 0;
    std::string soft_error;  // FrameParams que nao reproduzem os registradores usados (tem de ficar em 0)
    // N7b (Vulkan)
    std::string vk_device, vk_error;
    uint64_t    vk_frames = 0, vk_failures = 0, tex_uploads = 0, tex_legacy = 0, tex_evicted = 0;
    uint64_t    tex_replaced = 0, tex_replace_rejected = 0;  // substituicoes HD usadas / incompativeis (original)
    uint64_t    vk_fragment_retries = 0, vk_negative_depth = 0;  // listas por pixel refeitas; quadros com 1/w negativo
    uint64_t    vk_recoveries = 0;                                 // dispositivo recriado depois de uma perda
    double      vk_gpu_ms_total = 0, vk_gpu_ms_max = 0;           // tempo de GPU por quadro (total e o pior)
    uint64_t    vk_max_layers = 0;                                 // maior numero de camadas translucidas num pixel
    uint64_t    cmp_frames = 0, cmp_pixels = 0, cmp_diff = 0, cmp_diff8 = 0, cmp_worst_render = 0;
    uint32_t    cmp_max = 0;
    double      cmp_worst_fraction = 0;
    double      cmp_min_psnr = 0, cmp_psnr_sum = 0;  // PSNR (RGB) por quadro: minimo e soma (media = soma / quadros)
    uint64_t    cmp_below40 = 0;                     // quadros com PSNR < 40 dB (criterio do plano, secao 5)
    uint64_t distinct_texture_keys = 0, distinct_texture_ids = 0;  // chaves (TCW+tamanho) e ids de conteudo distintos
    // N8.7 3b (validacao): texturas que a geracao deu como iguais mas cujos bytes mudaram (tem de ficar em 0)
    uint64_t    tex_stale = 0;
    std::string tex_stale_error;
    // N8.7 (docs/N8-7-KAMUI.md, validacao): a IR do ListSink nativo contra a desta captura, render a render (tem de
    // ficar em 0 diferentes); os volumes fechados e os parametros contados pelos dois lados
    uint64_t    list_checked = 0, list_mismatch = 0;
    uint64_t    list_volume_inclusion = 0, list_volume_exclusion = 0, list_volume_or = 0;
    std::string list_error;
    // N7c: resolucao interna (Mode::Native com escala != 1)
    uint64_t hires_renders = 0, hires_shown = 0, hires_missed = 0;  // renders na escala; VBlanks com/sem a imagem maior
    double   hires_cpu_ms_total = 0;                                // leitura de volta (so a reduzida) na CPU
    uint32_t hires_w = 0, hires_h = 0;                              // tamanho do ultimo render na escala
    std::string first_error;
};

// Software (N8.7): o renderizador em software desenha a IR (native/render/soft); Native: Vulkan, com o software sobre a IR
// como recuo
enum class Mode : uint8_t { Off, Capture, Check, Native, Compare, Software };
void         install(Mode m, uint32_t every = 1);  // instala no pvr_render e zera as estatisticas (Off desinstala e descarta)
const Frame& last_frame();                         // a IR do ultimo render
const Stats& stats();
std::string  summary();
void         set_compare_dump(const std::string& dir, uint32_t every);  // BMP das comparacoes (Mode::Compare)
// N7c: resolucao interna do renderizador nativo (so Mode::Native; o modo comparar e a referencia continuam em 1). Com
// escala != 1 o Vulkan desenha a geometria em round(w x escala) x round(h x escala) (vk_device.h) e:
//   - o framebuffer emulado recebe essa imagem REDUZIDA na GPU para o tamanho do jogo (media dos pixels), entao o resto
//     do caminho (VRAM, capturas, hash dos quadros, a apresentacao SDL) continua coerente e e o recuo;
//   - a imagem maior fica na GPU (um slot do dispositivo compartilhado) para a apresentacao direta.
// 1 = o modo fiel. Vale a partir do proximo render; chamar na thread do jogo.
void  set_scale(float scale);
float scale();

// A imagem maior do que esta sendo EXIBIDO: `shown` e o quadro que o video do PVR leu da VRAM (pvr_video_frame). Se
// ele e um dos ultimos renders na escala (o conteudo confere com a imagem reduzida que foi para a VRAM, tolerando a
// quantizacao do formato do framebuffer), devolve o slot desse render; senao (filme, tela escrita pela CPU, escala 1),
// seq = 0. Thread do jogo.
struct HiresImage {
    uint64_t seq = 0;                    // slot no dispositivo compartilhado (VulkanDevice::last_slot)
    uint32_t width = 0, height = 0;      // tamanho da imagem maior
    uint32_t y0 = 0, rows = 0;           // as linhas que correspondem ao exibido
};
HiresImage hires_for(std::span<const uint32_t> shown, uint32_t width, uint32_t height);
// Le de volta as linhas exibidas de um slot (so para capturas e testes). false = o slot ja foi reaproveitado.
bool read_hires(const HiresImage& h, std::vector<uint32_t>& argb);

// Ponto de extensao das texturas HD (contrato do N7a): com provedor, o backend Vulkan troca cada textura pela
// substituicao do mesmo TextureId quando ela e compativel (replacement_compatible); senao, o original. nullptr =
// so originais (o padrao ate o N9, que traz o carregador de mods). O provedor tem de viver enquanto instalado.
void         set_texture_replacements(nr::ITextureReplacements* provider);
// Testes do recuo para o software: a proxima textura (Texture) ou o proximo quadro (Device) falha no Vulkan.
enum class Fault : uint8_t { None, Texture, Device, DeviceOnce, DeviceAgain, TextureUpload };  // Device: falha permanente; DeviceOnce: transitoria
void         inject_fault(Fault f);

// Diagnostico (BS_RENDER_IR_GUARDAR=<render>:<arquivo>): a IR inteira de um render (parametros, vertices, desenhos,
// volumes e as texturas ja convertidas, na ordem dos handles do quadro) num arquivo binario, gravada ANTES de ir ao
// dispositivo (sobrevive a uma perda do dispositivo nesse render); load_frame_dump a le de volta para reproduzir o
// render fora do jogo (tests/unit/test_render_vulkan.cpp, BS_RENDER_IR_REPRODUZIR).
struct DumpTexture {
    nr::TextureDesc       desc;
    std::vector<uint32_t> argb;
};
bool save_frame_dump(const std::string& path, const Frame& f);
bool load_frame_dump(const std::string& path, nr::CommandList& cl, std::vector<DumpTexture>& textures);

// Texels de uma textura com o fallback: as conversoes nativas ou, sem elas, o decodificador do legado.
bool texture_argb(const TextureKey& k, std::vector<uint32_t>& argb, bool& legacy);
// Os bytes que definem a textura na VRAM e a paleta usada (para o cache do backend saber se ela mudou).
void texture_source(const TextureKey& k, std::vector<uint8_t>& raw);
// N8.7 3b: o intervalo do caminho de 64 bits da textura, a geracao dele na memoria das texturas e se ela usa a paleta
void     texture_span(const TextureKey& k, uint32_t& base, uint32_t& n);
uint64_t texture_generation(const TextureKey& k);
bool     texture_palette(const TextureKey& k);

} // namespace bs::transitional::render_capture
