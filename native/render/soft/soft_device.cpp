// N8.7: o renderizador em software sobre a IR (soft_device.h).
#include "render/soft/soft_device.h"

#include "render/render_ir.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace bs::native::render::soft {
namespace {

uint32_t log2_size(uint32_t n) {  // 8 << k
    uint32_t k = 0;
    while ((8u << k) < n && k < 7) ++k;
    return k;
}

} // namespace

uint32_t pack_color(const Color& c) {
    auto q = [](float v) { return uint32_t(std::lround((v < 0 ? 0 : (v > 1 ? 1 : v)) * 255)); };
    return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}

Color unpack_color(uint32_t c) {
    return {((c >> 24) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f};
}

uint32_t list_number(ListType l) {
    switch (l) {
    case ListType::OpaqueModifier: return 1;
    case ListType::Translucent: return 2;
    case ListType::TranslucentModifier: return 3;
    case ListType::PunchThrough: return 4;
    default: return 0;
    }
}

Tri tri_state(const DrawState& s, uint32_t tcw, uint32_t tex_w, uint32_t tex_h) {
    Tri t;
    t.list = list_number(s.list);
    t.isp  = (uint32_t(s.depth) << 29) | (uint32_t(s.cull) << 27) | (uint32_t(!s.depth_write) << 26);
    const uint32_t flt = uint32_t(s.filter);
    uint32_t       env = 0;
    switch (s.env) {
    case TexEnv::Replace: env = 0; break;
    case TexEnv::Modulate: env = 1; break;
    case TexEnv::Decal: env = 2; break;
    case TexEnv::ModulateAlpha: env = 3; break;
    }
    t.tsp = (uint32_t(s.src) << 29) | (uint32_t(s.dst) << 26) | (uint32_t(s.fog) << 22) | (uint32_t(s.color_clamp) << 21) |
            (uint32_t(s.use_alpha) << 20) | (uint32_t(s.ignore_tex_alpha) << 19) |
            (uint32_t(s.wrap_u == Wrap::Mirror) << 18) | (uint32_t(s.wrap_v == Wrap::Mirror) << 17) |
            (uint32_t(s.wrap_u == Wrap::Clamp) << 16) | (uint32_t(s.wrap_v == Wrap::Clamp) << 15) | (flt << 13) |
            (uint32_t(s.src_select) << 25) | (uint32_t(s.dst_select) << 24) | (uint32_t(s.supersample) << 12) |
            (uint32_t(s.mip_bias & 0xF) << 8) | (env << 6);
    t.tex = bool(s.texture) && tex_w;
    if (t.tex) t.tcw = tcw, t.tsp |= (log2_size(tex_w) << 3) | log2_size(tex_h);
    t.offset   = s.offset_color;
    t.gouraud  = s.gouraud;
    t.shadow   = s.shadow;
    t.clipMode = s.clip == Clip::Inside ? 2 : (s.clip == Clip::Outside ? 3 : 0);
    if (s.clip != Clip::None) t.cx0 = s.clip_x0, t.cy0 = s.clip_y0, t.cx1 = s.clip_x1, t.cy1 = s.clip_y1;
    return t;
}

Vertex tri_vertex(const render::Vertex& v) {
    Vertex o;
    o.x = v.x, o.y = v.y, o.z = v.inv_w, o.u = v.u, o.v = v.v;
    o.col = unpack_color(v.color), o.off = unpack_color(v.offset);
    return o;
}

Frame tri_frame(const FrameParams& p, uint32_t tex_stride) {
    Frame r;
    r.background       = p.clear_color & 0xFFFFFF;
    r.background_depth = p.clear_depth;
    r.cull_val         = p.small_cull;
    r.tex_stride       = tex_stride;
    r.pt_ref           = uint32_t(std::lround(p.punch_through_ref * 255));  // byte / 255.f: a volta e exata
    r.half_offset      = (p.sample_center ? 1u : 0u) | (p.attribute_center ? 2u : 0u) | (p.texel_center ? 4u : 0u);
    r.clamp_max = p.clamp_max, r.clamp_min = p.clamp_min;
    r.autosort  = p.translucent_autosort;
    r.shadow_scale = p.shadow_scale;
    r.fog_table = p.fog_table, r.fog_color = p.fog_color & 0xFFFFFF, r.fog_density = p.fog_density;
    return r;
}

void tri_volumes(const CommandList& cl, std::vector<Volume>& vols, std::vector<VolumeTri>& tris) {
    vols.clear(), tris.clear();
    for (const ModifierVolume& m : cl.modifiers) {
        Volume v;
        v.first = uint32_t(tris.size()), v.count = m.triangle_count, v.list = list_number(m.list);
        v.inclusion = m.inclusion, v.closed = m.closed, v.cull = m.cull, v.or_from = m.or_from;
        for (uint32_t k = 0; k < m.triangle_count; ++k) {
            VolumeTri t;
            for (uint32_t i = 0; i < 3; ++i) {
                const render::Vertex& p = cl.vertices[m.first_vertex + 3 * k + i];
                t.x[i] = p.x, t.y[i] = p.y, t.z[i] = p.inv_w;
            }
            tris.push_back(t);
        }
        vols.push_back(v);
    }
}

// Os texels das texturas do dispositivo (no tamanho original: o TSP do desenho tem de dizer o mesmo tamanho)
class SoftwareDevice::Texels final : public TexelSource {
public:
    explicit Texels(const std::unordered_map<uint32_t, Tex>& t) : texs_(t) {}
    uint32_t texel(const TexRef& t, uint32_t x, uint32_t y) override {
        if (t.texture != lastId_) {
            const auto it = texs_.find(t.texture);
            last_ = it == texs_.end() ? nullptr : &it->second, lastId_ = t.texture;
        }
        return last_ ? last_->argb[size_t(y) * last_->width + x] : 0;
    }

private:
    const std::unordered_map<uint32_t, Tex>& texs_;
    const Tex*                               last_   = nullptr;
    uint32_t                                 lastId_ = 0;
};

Result<TextureHandle> SoftwareDevice::create_texture(const TextureDesc& desc, std::span<const std::byte> data) {
    if (desc.format != PixelFormat::ARGB8888 || !desc.width || !desc.height) return Error::Unsupported;
    const uint32_t id = ++next_;
    texs_[id]         = Tex{desc.width, desc.height, {}};
    TextureHandle h;
    h.value = id;
    if (auto r = update_texture(h, data); !r) {
        texs_.erase(id);
        return r.error();
    }
    return h;
}

Result<void> SoftwareDevice::update_texture(TextureHandle h, std::span<const std::byte> data) {
    const auto it = texs_.find(h.value);
    if (it == texs_.end()) return Error::InvalidHandle;
    Tex&         t     = it->second;
    const size_t texel = size_t(t.width) * t.height;
    if (data.size() < texel * 4) return Error::OutOfRange;
    t.argb.resize(texel);
    std::memcpy(t.argb.data(), data.data(), texel * 4);
    ++st_.uploads;
    return {};
}

void SoftwareDevice::destroy_texture(TextureHandle h) { texs_.erase(h.value); }

Result<void> SoftwareDevice::submit(const CommandList& cl) {
    if (!validate(cl, nullptr)) return Error::Corrupt;
    std::vector<Tri> tris;
    for (const Draw& d : cl.draws) {
        uint32_t tw = 0, th = 0;
        if (d.state.texture) {
            const auto it = texs_.find(d.state.texture.value);
            if (it == texs_.end()) return Error::InvalidHandle;  // nunca um desenho sem a textura dele
            tw = it->second.width, th = it->second.height;
            // o software amostra no tamanho original (o TSP): uma textura de outro tamanho (HD) e so do Vulkan
            if ((8u << log2_size(tw)) != tw || (8u << log2_size(th)) != th) return Error::Unsupported;
        }
        Tri base     = tri_state(d.state, 0, tw, th);
        base.texture = d.state.texture.value;
        for (uint32_t k = 0; k + 3 <= d.vertex_count; ++k) {
            Tri t = base;
            for (uint32_t i = 0; i < 3; ++i) t.v[i] = tri_vertex(cl.vertices[d.first_vertex + k + i]);
            if (k & 1) std::swap(t.v[0], t.v[1]);  // strip: os triangulos alternam o sentido
            tris.push_back(t);
        }
    }
    image_.width = cl.frame.width, image_.height = cl.frame.height;
    image_.color.clear(), image_.depth.clear();  // o fundo vem do quadro
    Texels                 texels(texs_);
    std::vector<Volume>    vols;
    std::vector<VolumeTri> vtris;
    tri_volumes(cl, vols, vtris);
    const RasterInfo info = rasterize(tris, vols, vtris, tri_frame(cl.frame, 0), texels, image_);
    st_.fragments += info.fragments, st_.shadowed_pixels += info.shadowed_pixels;
    st_.translucent_volumes_ignored += info.translucent_volumes_ignored;
    st_.frames++, st_.triangles += tris.size();
    return {};
}

Result<void> SoftwareDevice::present_image(std::span<const uint32_t> argb, uint32_t width, uint32_t height) {
    if (argb.size() < size_t(width) * height) return Error::OutOfRange;
    image_.width = width, image_.height = height;
    image_.color.assign(argb.begin(), argb.begin() + size_t(width) * height);
    image_.depth.clear();
    return {};
}

Result<void> SoftwareDevice::read_frame(std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height) const {
    if (image_.color.empty()) return Error::NotFound;
    argb = image_.color, width = image_.width, height = image_.height;
    return {};
}

} // namespace bs::native::render::soft
