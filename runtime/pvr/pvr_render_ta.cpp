// N8.7: o rasterizador do caminho do TA (a referencia das comparacoes e do modo conferir), so no build de validacao
// (papel validacao): os triangulos do decodificador do TA desenhados pelo rasterizador em software (native/render/soft)
// com os texels lidos da VRAM (pvr_texel). O produto desenha pela IR (transitional/render_capture, modo Software).
#include "pvr/pvr_render.h"

#include "pvr/pvr_texture.h"

#include <map>
#include <tuple>
#include <vector>

namespace bs {
namespace {

// Os texels da VRAM (o caminho do TA): ja decodificados neste render (a VRAM e a paleta nao mudam durante um render),
// preenchidos sob demanda, so os texels usados. Mesmo valor de pvr_texel.
class VramTexels final : public native::render::soft::TexelSource {
public:
    uint32_t texel(const native::render::soft::TexRef& t, uint32_t x, uint32_t y) override {
        if (!last_ || t.tcw != lastKey_[0] || t.width != lastKey_[1] || t.height != lastKey_[2] || t.stride != lastKey_[3]) {
            Cache& e = cache_[{t.tcw, t.width, t.height, t.stride}];
            if (e.argb.empty()) {
                e.argb.resize(size_t(t.width) * t.height);
                e.have.assign(size_t(t.width) * t.height, 0);
            }
            last_ = &e, lastKey_[0] = t.tcw, lastKey_[1] = t.width, lastKey_[2] = t.height, lastKey_[3] = t.stride;
        }
        const size_t i = size_t(y) * t.width + x;
        if (!last_->have[i]) {
            last_->argb[i] = pvr_texel(TexInfo{t.tcw, t.width, t.height, t.stride}, x, y);
            last_->have[i] = 1;
        }
        return last_->argb[i];
    }

private:
    struct Cache {
        std::vector<uint32_t> argb;
        std::vector<uint8_t>  have;
    };
    std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, Cache> cache_;
    Cache*   last_ = nullptr;
    uint32_t lastKey_[4] = {};
};

} // namespace

size_t pvr_render_ta(std::span<const PvrTri> prims, std::span<const native::render::soft::Volume> vols,
                     std::span<const native::render::soft::VolumeTri> vtris, const PvrFrame& fr, native::render::soft::Image& r,
                     native::render::soft::PhaseFn phase, void* ctx) {
    VramTexels texels;
    return size_t(native::render::soft::rasterize(prims, vols, vtris, fr, texels, r, phase, ctx).fragments);
}

std::vector<uint32_t> pvr_render_triangles(std::span<const PvrTri> tris, std::span<const native::render::soft::Volume> vols,
                                           std::span<const native::render::soft::VolumeTri> vtris, const PvrFrame& frame,
                                           uint32_t width, uint32_t height) {
    VramTexels                     texels;
    native::render::soft::Image img;
    img.width = width, img.height = height;
    native::render::soft::rasterize(tris, vols, vtris, frame, texels, img);
    return std::move(img.color);
}

} // namespace bs
