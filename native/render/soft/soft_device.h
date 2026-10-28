#pragma once
// N8.7: o renderizador em software sobre a IR (o recuo permanente do Vulkan e a referencia das comparacoes). Recebe a
// CommandList como o backend Vulkan (IRenderDevice) e desenha com o rasterizador de referencia (soft_raster.h), com os
// texels das texturas que recebeu (nenhuma VRAM, registrador ou endereco do guest).
//
// A IR volta para as palavras do hardware (ISP/TSP/TCW) com as funcoes abaixo, as mesmas que o modo `conferir` da
// captura (transitional/render_capture) usa para provar que a IR reproduz o caminho do TA.
#include "render/render.h"
#include "render/soft/soft_raster.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bs::native::render::soft {

uint32_t pack_color(const Color& c);
Color    unpack_color(uint32_t argb);  // x / 255.f, como o decodificador do TA: a ida e a volta sao exatas

uint32_t list_number(ListType l);
// O estado de um desenho nas palavras do hardware. tcw/tex_w/tex_h: a textura (tex_w = 0: sem textura); o tamanho vai
// para os bits do TSP (8 << k).
Tri    tri_state(const DrawState& s, uint32_t tcw, uint32_t tex_w, uint32_t tex_h);
Vertex tri_vertex(const render::Vertex& v);
// Os parametros do quadro (tex_stride: o TEXT_CONTROL, so para as texturas lineares com stride do caminho do TA).
Frame  tri_frame(const FrameParams& p, uint32_t tex_stride);
// N8.7: os volumes modificadores da IR para o rasterizador (os triangulos de x, y e 1/w)
void   tri_volumes(const CommandList& cl, std::vector<Volume>& vols, std::vector<VolumeTri>& tris);

struct SoftStats {
    uint64_t frames = 0, triangles = 0, fragments = 0, uploads = 0;
    uint64_t shadowed_pixels = 0, translucent_volumes_ignored = 0;  // N8.7
};

class SoftwareDevice final : public IRenderDevice {
public:
    Result<TextureHandle> create_texture(const TextureDesc& desc, std::span<const std::byte> data) override;
    Result<void>          update_texture(TextureHandle t, std::span<const std::byte> data) override;
    void                  set_palette(std::span<const uint32_t>) override {}  // as texturas chegam ja em ARGB8888
    void                  destroy_texture(TextureHandle t) override;
    Result<void>          submit(const CommandList& frame) override;
    Result<void>          present_image(std::span<const uint32_t> argb, uint32_t width, uint32_t height) override;
    Result<void>          present() override { return {}; }
    void                  resize(uint32_t, uint32_t) override {}

    // A imagem do ultimo submit/present_image: ARGB8888, width x height, de cima para baixo.
    Result<void>     read_frame(std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height) const;
    const SoftStats& stats() const { return st_; }

private:
    struct Tex {
        uint32_t              width = 0, height = 0;
        std::vector<uint32_t> argb;
    };
    class Texels;
    std::unordered_map<uint32_t, Tex> texs_;
    uint32_t                          next_ = 0;
    Image                             image_;
    SoftStats                         st_;
};

} // namespace bs::native::render::soft
