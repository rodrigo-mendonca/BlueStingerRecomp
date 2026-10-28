#pragma once
// Renderizacao (N7a/N7b). A IR de comandos captura o que hoje chega ao TA (store queues,
// DMA, escrita direta) como listas de desenho nativas; o dispositivo (Vulkan, decisao do
// mantenedor de 2026-10-19: docs/DECISAO-VULKAN-LINUX.md) as executa. Os conceitos do PowerVR que importam para a imagem (listas
// opaca/translucida/punch-through, volumes modificadores, cor de deslocamento, fog por
// tabela) aparecem como estado de desenho, sem registradores nem enderecos de VRAM.
#include "core/handle.h"
#include "core/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bs::native::render {

struct TextureTag;
using TextureHandle = Handle<TextureTag>;

enum class PixelFormat : uint8_t {
    ARGB1555, RGB565, ARGB4444, YUV422, ARGB8888,
    Palette4, Palette8,  // indice em paleta (Paleta separada)
};

struct TextureDesc {
    uint32_t    width = 0, height = 0;
    PixelFormat format = PixelFormat::ARGB8888;
    uint32_t    mip_levels = 1;  // 1 = sem mipmap
};

enum class ListType : uint8_t { Opaque, OpaqueModifier, Translucent, TranslucentModifier, PunchThrough };
enum class Blend : uint8_t { Zero, One, OtherColor, InvOtherColor, SrcAlpha, InvSrcAlpha, DstAlpha, InvDstAlpha };
enum class DepthTest : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
// ISP bits 28-27 (revisao Codex 2026-10-21): 2 descarta o anti-horario na tela, 3 o horario (com y para baixo, a area
// (b - a) x (c - a) do rasterizador de referencia e negativa no anti-horario)
enum class Cull : uint8_t { None, Small, CounterClockwise, Clockwise };
// TSP bits 14-13: os dois modos trilineares (passadas A e B do hardware) sao distintos
enum class Filter : uint8_t { Point, Bilinear, TrilinearA, TrilinearB };
enum class Wrap : uint8_t { Repeat, Clamp, Mirror };
enum class TexEnv : uint8_t { Replace, Modulate, Decal, ModulateAlpha };
enum class Fog : uint8_t { Table, Vertex, None, Table2 };
enum class Clip : uint8_t { None, Inside, Outside };  // recorte do usuario: so dentro / so fora do retangulo

struct DrawState {
    ListType      list = ListType::Opaque;
    TextureHandle texture;  // vazio = sem textura
    Blend         src = Blend::One, dst = Blend::Zero;
    DepthTest     depth = DepthTest::GreaterEqual;  // profundidade 1/w (maior = mais perto)
    bool          depth_write = true;
    Cull          cull = Cull::None;
    Filter        filter = Filter::Bilinear;
    Wrap          wrap_u = Wrap::Repeat, wrap_v = Wrap::Repeat;
    TexEnv        env = TexEnv::Modulate;
    Fog           fog = Fog::None;
    bool          gouraud = true;
    bool          use_alpha = false;
    bool          offset_color = false;  // soma a cor de deslocamento (especular)
    bool          ignore_tex_alpha = false;  // alfa da textura tratado como 1
    bool          color_clamp = false;       // cor final limitada por FrameParams::clamp_min/max
    // N8.7: PCW bit 7. Dentro de um volume modificador da fase do desenho, a cor e escalada por
    // FrameParams::shadow_scale (o modo de um volume; o jogo nao usa "dois volumes", PCW bit 6)
    bool          shadow = false;
    // bits do TSP que o rasterizador de referencia ainda nao usa, guardados para o backend (revisao Codex 2026-10-21)
    uint8_t       mip_bias = 0;              // TSP bits 11-8 (valor bruto)
    bool          supersample = false;       // TSP bit 12
    bool          src_select = false, dst_select = false;  // TSP bits 25/24: buffer secundario de acumulacao
    Clip          clip = Clip::None;
    int32_t       clip_x0 = 0, clip_y0 = 0, clip_x1 = 0, clip_y1 = 0;  // pixels, inclusivo
    bool          operator==(const DrawState&) const = default;
};

struct Vertex {
    float    x = 0, y = 0;  // tela (pixels)
    float    inv_w = 0;     // 1/w (profundidade)
    float    u = 0, v = 0;
    uint32_t color  = 0xFFFFFFFF;  // ARGB
    uint32_t offset = 0;           // ARGB da cor de deslocamento
};

// Faixa de vertices (triangle strip) com um estado.
struct Draw {
    DrawState state;
    uint32_t  first_vertex = 0, vertex_count = 0;
};

// Volume modificador (sombra/regiao): triangulos que alteram os desenhos marcados. A lista diz a fase:
// OpaqueModifier (lista 1 do TA: age sobre opacos e punch-through) ou TranslucentModifier (lista 3: age
// sobre os translucidos). Dentro da lista, a ordem de submissao.
struct ModifierVolume {
    uint32_t first_vertex = 0, triangle_count = 0;
    bool     inclusion = true;  // dentro do volume (senao fora)
    ListType list = ListType::OpaqueModifier;
    // N8.7: o descarte de faces do volume (ISP bits 28-27, como Cull), se ele foi fechado (o ultimo parametro com a
    // instrucao 1 ou 2; aberto = so os triangulos, sem somar a area) e o primeiro triangulo que entra por OU em vez da
    // inversao (o parametro de fechamento sem o bit "volume" do PCW: um volume aberto ou um quad). ~0u = nenhum.
    uint8_t  cull = 0;
    bool     closed = false;
    uint32_t or_from = ~0u;
};

struct FrameParams {
    uint32_t                width = 640, height = 480;
    uint32_t                clear_color = 0xFF000000;
    float                   clear_depth = 0;  // 1/w do fundo
    std::array<uint16_t, 128> fog_table{};  // FOG_TABLE: 2 valores de 8 bits por entrada (bruto)
    uint32_t                fog_color = 0;         // FOG_COL_RAM (fog por tabela)
    uint32_t                fog_vertex_color = 0;  // FOG_COL_VERT (fog por vertice)
    uint32_t                fog_density = 0;       // FOG_DENSITY (bruto: mantissa e expoente)
    uint32_t                shadow_scale = 0;      // N8.7: FPU_SHAD_SCALE (bruto: bit 8 habilita, bits 7-0 a escala)
    float                   punch_through_ref = 0.5f;  // limiar de alfa
    // regras do rasterizador de referencia que valem para o quadro inteiro (N7a)
    bool                    translucent_autosort = true;  // translucidos ordenados por pixel (senao, na submissao)
    bool                    sample_center = true;         // amostra de cobertura/profundidade no centro do pixel
    bool                    attribute_center = true;      // amostra dos atributos no centro do pixel
    bool                    texel_center = true;          // meio texel nas coordenadas de textura
    float                   small_cull = 0;               // Cull::Small: area minima (a do hardware, em pixels)
    uint32_t                clamp_min = 0, clamp_max = 0xFFFFFFFF;  // ARGB, DrawState::color_clamp
};

struct CommandList {
    FrameParams                 frame;
    std::vector<Vertex>         vertices;
    std::vector<Draw>           draws;      // na ordem de submissao, por lista
    std::vector<ModifierVolume> modifiers;
};

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    virtual Result<TextureHandle> create_texture(const TextureDesc& desc, std::span<const std::byte> mip_chain) = 0;
    virtual Result<void> update_texture(TextureHandle t, std::span<const std::byte> mip_chain) = 0;
    virtual void         set_palette(std::span<const uint32_t> argb) = 0;  // para Palette4/Palette8
    virtual void         destroy_texture(TextureHandle t) = 0;
    virtual Result<void> submit(const CommandList& frame) = 0;
    // Quadro inteiro ja pronto (FMV, telas de carga): sem geometria.
    virtual Result<void> present_image(std::span<const uint32_t> argb, uint32_t width, uint32_t height) = 0;
    virtual Result<void> present() = 0;
    virtual void         resize(uint32_t width, uint32_t height) = 0;
};

} // namespace bs::native::render
