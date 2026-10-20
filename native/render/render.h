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
enum class Cull : uint8_t { None, Small, Clockwise, CounterClockwise };
enum class Filter : uint8_t { Point, Bilinear, Trilinear };
enum class Wrap : uint8_t { Repeat, Clamp, Mirror };
enum class TexEnv : uint8_t { Replace, Modulate, Decal, ModulateAlpha };
enum class Fog : uint8_t { Table, Vertex, None, Table2 };

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

// Volume modificador (sombra/regiao): triangulos que alteram os desenhos marcados.
struct ModifierVolume {
    uint32_t first_vertex = 0, triangle_count = 0;
    bool     inclusion = true;  // dentro do volume (senao fora)
};

struct FrameParams {
    uint32_t                width = 640, height = 480;
    uint32_t                clear_color = 0xFF000000;
    std::array<uint8_t, 128> fog_table{};  // densidade por faixa de profundidade
    uint32_t                fog_color = 0;
    float                   punch_through_ref = 0.5f;  // limiar de alfa
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
