#include "render_capture.h"

#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "render/render_ir.h"
#include "render/texture_convert.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <utility>

namespace bs::transitional::render_capture {
namespace {

constexpr uint32_t R(uint32_t off) { return 0x005F8000 + off; }

float f32(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

uint32_t pack(const PvrColor& c) {
    auto q = [](float v) { return uint32_t(std::lround((v < 0 ? 0 : (v > 1 ? 1 : v)) * 255)); };
    return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}
PvrColor unpack(uint32_t c) {  // a mesma expressao do decodificador (x / 255.f): o round-trip e exato
    return {((c >> 24) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f};
}

nr::ListType list_type(uint32_t l) {
    switch (l) {
    case 1: return nr::ListType::OpaqueModifier;
    case 2: return nr::ListType::Translucent;
    case 3: return nr::ListType::TranslucentModifier;
    case 4: return nr::ListType::PunchThrough;
    default: return nr::ListType::Opaque;
    }
}
uint32_t list_number(nr::ListType l) {
    switch (l) {
    case nr::ListType::OpaqueModifier: return 1;
    case nr::ListType::Translucent: return 2;
    case nr::ListType::TranslucentModifier: return 3;
    case nr::ListType::PunchThrough: return 4;
    default: return 0;
    }
}

uint32_t log2_size(uint32_t n) {  // 8 << k
    uint32_t k = 0;
    while ((8u << k) < n && k < 7) ++k;
    return k;
}

TextureKey key_of(const PvrTri& t, uint32_t stride) {
    return {t.tcw, 8u << ((t.tsp >> 3) & 7), 8u << (t.tsp & 7), stride};
}

bool same_vertex(const PvrVertex& a, const PvrVertex& b) {
    return std::memcmp(&a, &b, sizeof a) == 0;  // exato: os dois lados sao os mesmos floats
}

class Capture final : public PvrCaptureSink {
public:
    Mode     mode  = Mode::Off;
    uint32_t every = 1;
    Frame    frame;
    Stats    st;

    void begin(uint32_t w, uint32_t h) override {
        frame         = {};
        frame.cl.frame = frame_params(w, h);
        stride_       = (pvr_reg(R(0x0E4)) & 0x1F) * 32;  // TEXT_CONTROL
        frame.texture_stride = stride_;
        reset_strip();
        mod_open_ = false, mod_closing_ = false, mod_cur_closed_ = false;  // os volumes recomecam a cada render
        mod_instr_ = 0, mod_closed_ = 0;
    }

    void vertex(const PvrTri& state, const PvrVertex& v, bool end) override {
        strip_.push_back(v);
        const size_t n = strip_.size();
        if (n >= 3) {
            const nr::DrawState s = state_of(state);
            const bool contiguous = open_ >= 0 && frame.cl.draws[size_t(open_)].first_vertex +
                                                          frame.cl.draws[size_t(open_)].vertex_count ==
                                                      frame.cl.vertices.size();  // os vertices do strip sao os ultimos
            if (!tri_mode_ && contiguous && s == frame.cl.draws[size_t(open_)].state) {
                frame.cl.draws[size_t(open_)].vertex_count++;  // o strip continua com o mesmo estado
                frame.cl.vertices.push_back(render_capture::vertex(v));
            } else if (!tri_mode_ && n == 3) {  // o primeiro triangulo do strip abre o desenho
                open_ = int32_t(frame.cl.draws.size());
                frame.cl.draws.push_back({s, uint32_t(frame.cl.vertices.size()), 3});
                for (size_t i = 0; i < 3; ++i) frame.cl.vertices.push_back(render_capture::vertex(strip_[i]));
            } else {
                // estado mudou no meio do strip (recorte do usuario entre vertices): daqui ate o fim do strip, um
                // desenho por triangulo, na ordem exata do decodificador (n par: os dois primeiros trocados)
                tri_mode_ = true, open_ = -1;
                PvrVertex t[3] = {strip_[n - 3], strip_[n - 2], strip_[n - 1]};
                if (n % 2 == 0) std::swap(t[0], t[1]);
                frame.cl.draws.push_back({s, uint32_t(frame.cl.vertices.size()), 3});
                for (const PvrVertex& x : t) frame.cl.vertices.push_back(render_capture::vertex(x));
            }
        }
        if (end) reset_strip();
    }

    void strip_break() override { reset_strip(); }

    void sprite(const PvrTri& state, const PvrVertex& a, const PvrVertex& b, const PvrVertex& c,
                const PvrVertex& d) override {
        // dois desenhos de 3 vertices, (A, B, C) e (A, C, D), na ordem do decodificador: um strip de 4 so os
        // reproduz com os vertices numa ordem ciclica diferente, o que muda as somas da interpolacao
        const nr::DrawState s = state_of(state);
        const PvrVertex* t1[3] = {&a, &b, &c};
        const PvrVertex* t2[3] = {&a, &c, &d};
        for (const PvrVertex* const* t : {t1, t2}) {
            frame.cl.draws.push_back({s, uint32_t(frame.cl.vertices.size()), 3});
            for (int i = 0; i < 3; ++i) frame.cl.vertices.push_back(render_capture::vertex(*t[i]));
        }
        ++frame.sprites;
        open_ = -1;
    }

    void global(const TaParam& g) override {
        // volume modificador: a instrucao (ISP bits 30-29 do global) 0 = poligono do volume, 1 = ultimo poligono
        // (inclusao), 2 = ultimo poligono (exclusao). Os triangulos depois de um global 1/2 fecham o volume; o
        // proximo global (de qualquer lista) comeca outro (revisao Codex 2026-10-21: globais iguais nao se fundem)
        if (mod_closing_) mod_open_ = false;
        mod_closing_ = false;
        if (g.list != 1 && g.list != 3) return;
        mod_instr_   = (g.w[1] >> 29) & 3;
        mod_closing_ = mod_instr_ == 1 || mod_instr_ == 2;
    }

    void modifier(const TaParam&, const TaParam& p) override {
        // vertice de volume modificador (64 bytes): A, B, C em x, y, z nas palavras 1..9
        ++frame.modifier_params;
        if (!mod_open_ || frame.cl.modifiers.back().list != list_type(p.list)) {
            frame.cl.modifiers.push_back({uint32_t(frame.cl.vertices.size()), 0, true, list_type(p.list)});
            ++frame.modifier_volumes;
            mod_open_ = true, mod_cur_closed_ = false;
        }
        if (mod_closing_) {
            frame.cl.modifiers.back().inclusion = mod_instr_ == 1;
            if (!mod_cur_closed_) mod_cur_closed_ = true, ++mod_closed_;
        }
        for (int i = 0; i < 3; ++i) {
            nr::Vertex v;
            v.x = f32(p.w[1 + 3 * i]), v.y = f32(p.w[2 + 3 * i]), v.inv_w = f32(p.w[3 + 3 * i]);
            frame.cl.vertices.push_back(v);
        }
        frame.cl.modifiers.back().triangle_count++;
    }

    void skipped(const TaParam&) override { ++frame.skipped_params; }

    void end(const PvrFrame& used, std::span<const PvrTri> drawn, std::span<const uint32_t> image, uint32_t w,
             uint32_t h) override {
        frame.modifier_unclosed = frame.modifier_volumes - mod_closed_;
        st.modifier_unclosed += frame.modifier_unclosed;
        ++st.renders;
        st.draws += frame.cl.draws.size(), st.vertices += frame.cl.vertices.size(), st.sprites += frame.sprites;
        st.modifier_params += frame.modifier_params, st.modifier_volumes += frame.modifier_volumes;
        st.skipped_params += frame.skipped_params;
        if (mode != Mode::Check || (st.renders - 1) % every) return;
        ++st.checked;
        auto fail = [&](uint64_t& counter, const std::string& what) {
            ++counter;
            if (st.first_error.empty()) st.first_error = "render " + std::to_string(st.renders) + ": " + what;
        };
        std::string err;
        if (!valid(frame, &err)) {
            fail(st.invalid, "IR invalida: " + err);
            return;  // indices nao confiaveis: nada mais e percorrido
        }
        const PvrFrame from_ir = pvr_frame(frame);
        if (!(from_ir == used)) fail(st.frame_mismatch, "FrameParams nao reproduzem os registradores do quadro");
        const std::vector<PvrTri> tris = triangles(frame);
        st.triangles += tris.size();
        if (tris.size() != drawn.size()) {
            fail(st.triangle_mismatch, "IR com " + std::to_string(tris.size()) + " triangulos, rasterizador " +
                                           std::to_string(drawn.size()));
        } else {
            for (size_t i = 0; i < tris.size(); ++i) {
                const PvrTri& a = tris[i];
                const PvrTri& b = drawn[i];
                const bool    vtx = same_vertex(a.v[0], b.v[0]) && same_vertex(a.v[1], b.v[1]) && same_vertex(a.v[2], b.v[2]);
                const bool    est = a.list == b.list && state_of(a) == state_of(b) &&
                                 (!b.tex || key_of(a, stride_) == key_of(b, stride_));
                if (!vtx || !est) {
                    fail(st.triangle_mismatch, "triangulo " + std::to_string(i) + (vtx ? " (estado)" : " (vertices)"));
                    break;
                }
            }
        }
        const std::vector<uint32_t> again = pvr_render_triangles(tris, from_ir, w, h);
        if (again.size() != image.size() || std::memcmp(again.data(), image.data(), image.size() * 4) != 0) {
            size_t diff = 0;
            for (size_t i = 0; i < std::min(again.size(), image.size()); ++i) diff += again[i] != image[i];
            fail(st.image_mismatch, "imagem a partir da IR difere em " + std::to_string(diff) + " pixels");
        }
        for (const TextureKey& k : frame.textures) {
            std::vector<uint32_t> argb;
            if (!convert_texture(k, argb)) {
                ++st.textures_without_native;
                continue;
            }
            ++st.textures_checked;
            const nr::TextureId id = nr::texture_id(k.width, k.height, argb);
            if (keys_.insert({k.tcw, (uint64_t(k.width) << 32) | k.height}).second) ++st.distinct_texture_keys;
            if (ids_.insert({id.content, (uint64_t(id.width) << 32) | id.height}).second) ++st.distinct_texture_ids;
            const TexInfo t{k.tcw, k.width, k.height, k.stride};
            for (uint32_t y = 0, bad = 0; y < k.height && !bad; ++y)
                for (uint32_t x = 0; x < k.width; ++x)
                    if (argb[size_t(y) * k.width + x] != pvr_texel(t, x, y)) {
                        char b[96];
                        std::snprintf(b, sizeof b, "textura TCW %08X %ux%u: texel (%u,%u)", k.tcw, k.width, k.height, x, y);
                        fail(st.texture_mismatch, b);
                        bad = 1;
                        break;
                    }
        }
    }

private:
    std::vector<PvrVertex> strip_;
    int32_t                open_     = -1;
    bool                   tri_mode_ = false;
    uint32_t               stride_   = 0;
    bool                   mod_open_ = false, mod_closing_ = false, mod_cur_closed_ = false;
    uint32_t               mod_instr_ = 0, mod_closed_ = 0;  // instrucao do ultimo global; volumes com ultimo poligono
    std::set<std::pair<uint64_t, uint64_t>> keys_, ids_;  // para o resumo: quantas chaves e quantos conteudos

    void reset_strip() {
        strip_.clear();
        open_ = -1, tri_mode_ = false;
    }

    nr::DrawState state_of(const PvrTri& t) {
        uint32_t handle = 0;
        if (t.tex) {
            const TextureKey k = key_of(t, stride_);
            size_t           i = 0;
            while (i < frame.textures.size() && !(frame.textures[i] == k)) ++i;
            if (i == frame.textures.size()) frame.textures.push_back(k);
            handle = uint32_t(i + 1);
        }
        return draw_state(t, handle);
    }
};

std::unique_ptr<Capture> g_cap;

} // namespace

nr::DrawState draw_state(const PvrTri& t, uint32_t texture_handle) {
    nr::DrawState s;
    s.list          = list_type(t.list);
    s.texture.value = t.tex ? texture_handle : 0;
    s.src           = nr::Blend(t.tsp >> 29);
    s.dst           = nr::Blend((t.tsp >> 26) & 7);
    s.depth         = nr::DepthTest(t.isp >> 29);
    s.depth_write   = !((t.isp >> 26) & 1);
    s.cull          = nr::Cull((t.isp >> 27) & 3);
    s.filter = nr::Filter((t.tsp >> 13) & 3);
    s.wrap_u = ((t.tsp >> 16) & 1) ? nr::Wrap::Clamp : (((t.tsp >> 18) & 1) ? nr::Wrap::Mirror : nr::Wrap::Repeat);
    s.wrap_v = ((t.tsp >> 15) & 1) ? nr::Wrap::Clamp : (((t.tsp >> 17) & 1) ? nr::Wrap::Mirror : nr::Wrap::Repeat);
    static constexpr nr::TexEnv kEnv[4] = {nr::TexEnv::Replace, nr::TexEnv::Modulate, nr::TexEnv::Decal,
                                           nr::TexEnv::ModulateAlpha};  // decal, modulate, decal alpha, modulate alpha
    s.env              = kEnv[(t.tsp >> 6) & 3];
    s.fog              = nr::Fog((t.tsp >> 22) & 3);
    s.gouraud          = t.gouraud;
    s.use_alpha        = (t.tsp >> 20) & 1;
    s.offset_color     = t.offset;
    s.ignore_tex_alpha = (t.tsp >> 19) & 1;
    s.color_clamp      = (t.tsp >> 21) & 1;
    s.mip_bias         = uint8_t((t.tsp >> 8) & 0xF);
    s.supersample      = (t.tsp >> 12) & 1;
    s.src_select       = (t.tsp >> 25) & 1;
    s.dst_select       = (t.tsp >> 24) & 1;
    s.clip             = t.clipMode == 2 ? nr::Clip::Inside : (t.clipMode == 3 ? nr::Clip::Outside : nr::Clip::None);
    if (s.clip != nr::Clip::None) s.clip_x0 = t.cx0, s.clip_y0 = t.cy0, s.clip_x1 = t.cx1, s.clip_y1 = t.cy1;
    return s;
}

PvrTri pvr_state(const nr::DrawState& s, const TextureKey* tex) {
    PvrTri t;
    t.list = list_number(s.list);
    t.isp  = (uint32_t(s.depth) << 29) | (uint32_t(s.cull) << 27) | (uint32_t(!s.depth_write) << 26);
    const uint32_t flt = uint32_t(s.filter);
    uint32_t       env = 0;
    switch (s.env) {
    case nr::TexEnv::Replace: env = 0; break;
    case nr::TexEnv::Modulate: env = 1; break;
    case nr::TexEnv::Decal: env = 2; break;
    case nr::TexEnv::ModulateAlpha: env = 3; break;
    }
    t.tsp = (uint32_t(s.src) << 29) | (uint32_t(s.dst) << 26) | (uint32_t(s.fog) << 22) | (uint32_t(s.color_clamp) << 21) |
            (uint32_t(s.use_alpha) << 20) | (uint32_t(s.ignore_tex_alpha) << 19) |
            (uint32_t(s.wrap_u == nr::Wrap::Mirror) << 18) | (uint32_t(s.wrap_v == nr::Wrap::Mirror) << 17) |
            (uint32_t(s.wrap_u == nr::Wrap::Clamp) << 16) | (uint32_t(s.wrap_v == nr::Wrap::Clamp) << 15) | (flt << 13) |
            (uint32_t(s.src_select) << 25) | (uint32_t(s.dst_select) << 24) | (uint32_t(s.supersample) << 12) |
            (uint32_t(s.mip_bias & 0xF) << 8) | (env << 6);
    t.tex = bool(s.texture) && tex;
    if (t.tex) t.tcw = tex->tcw, t.tsp |= (log2_size(tex->width) << 3) | log2_size(tex->height);
    t.offset   = s.offset_color;
    t.gouraud  = s.gouraud;
    t.clipMode = s.clip == nr::Clip::Inside ? 2 : (s.clip == nr::Clip::Outside ? 3 : 0);
    if (s.clip != nr::Clip::None) t.cx0 = s.clip_x0, t.cy0 = s.clip_y0, t.cx1 = s.clip_x1, t.cy1 = s.clip_y1;
    return t;
}

nr::Vertex vertex(const PvrVertex& v) {
    nr::Vertex o;
    o.x = v.x, o.y = v.y, o.inv_w = v.z, o.u = v.u, o.v = v.v;
    o.color = pack(v.col), o.offset = pack(v.off);
    return o;
}

PvrVertex pvr_vertex(const nr::Vertex& v) {
    PvrVertex o;
    o.x = v.x, o.y = v.y, o.z = v.inv_w, o.u = v.u, o.v = v.v;
    o.col = unpack(v.color), o.off = unpack(v.offset);
    return o;
}

nr::FrameParams frame_params(uint32_t width, uint32_t height) {
    nr::FrameParams f;
    f.width = width, f.height = height;
    f.clear_color = 0xFF000000 | pvr_background_color();
    f.clear_depth = f32(pvr_reg(R(0x088)));  // ISP_BACKGND_D
    for (size_t i = 0; i < f.fog_table.size(); ++i) f.fog_table[i] = uint16_t(pvr_reg(R(0x200 + uint32_t(i) * 4)) & 0xFFFF);
    f.fog_color             = pvr_reg(R(0x0B0));  // FOG_COL_RAM
    f.fog_vertex_color      = pvr_reg(R(0x0B4));  // FOG_COL_VERT
    f.fog_density           = pvr_reg(R(0x0B8)) & 0xFFFF;  // FOG_DENSITY
    f.punch_through_ref     = float(pvr_reg(R(0x11C)) & 0xFF) / 255.f;
    f.translucent_autosort  = !(pvr_reg(R(0x098)) & 1);  // ISP_FEED_CFG bit 0
    const uint32_t half     = pvr_reg(R(0x080));           // HALF_OFFSET
    f.sample_center         = half & 1, f.attribute_center = half & 2, f.texel_center = half & 4;
    f.small_cull            = f32(pvr_reg(R(0x078)));      // FPU_CULL_VAL
    f.clamp_max             = pvr_reg(R(0x0BC)), f.clamp_min = pvr_reg(R(0x0C0));
    return f;
}

PvrFrame pvr_frame(const Frame& f) {
    const nr::FrameParams& p = f.cl.frame;
    PvrFrame               r;
    r.background       = p.clear_color & 0xFFFFFF;
    r.background_depth = p.clear_depth;
    r.cull_val         = p.small_cull;
    r.tex_stride       = f.texture_stride;
    r.pt_ref           = uint32_t(std::lround(p.punch_through_ref * 255));  // byte / 255.f: a volta e exata
    r.half_offset      = (p.sample_center ? 1u : 0u) | (p.attribute_center ? 2u : 0u) | (p.texel_center ? 4u : 0u);
    r.clamp_max = p.clamp_max, r.clamp_min = p.clamp_min;
    r.autosort  = p.translucent_autosort;
    return r;
}

bool valid(const Frame& f, std::string* error) {
    if (!nr::validate(f.cl, error)) return false;
    for (size_t i = 0; i < f.cl.draws.size(); ++i)
        if (f.cl.draws[i].state.texture.value > f.textures.size()) {
            if (error) *error = "desenho " + std::to_string(i) + ": textura fora da tabela do quadro";
            return false;
        }
    return true;
}

std::vector<PvrTri> triangles(const Frame& f) {
    std::vector<PvrTri> out;
    for (const nr::Draw& d : f.cl.draws) {
        const TextureKey* tex = d.state.texture ? &f.textures[d.state.texture.value - 1] : nullptr;
        const PvrTri      base = pvr_state(d.state, tex);
        for (uint32_t k = 0; k + 3 <= d.vertex_count; ++k) {
            PvrTri t = base;
            for (uint32_t i = 0; i < 3; ++i) t.v[i] = pvr_vertex(f.cl.vertices[d.first_vertex + k + i]);
            if (k & 1) std::swap(t.v[0], t.v[1]);  // strip: os triangulos alternam o sentido
            out.push_back(t);
        }
    }
    return out;
}

bool convert_texture(const TextureKey& k, std::vector<uint32_t>& argb) {
    const uint32_t fmt = (k.tcw >> 27) & 7, base = (k.tcw & 0x1FFFFF) << 3;
    const bool     mip = k.tcw >> 31, vq = (k.tcw >> 30) & 1, linear = (k.tcw >> 26) & 1, strideSel = (k.tcw >> 25) & 1;
    const uint32_t w = k.width, h = k.height;
    auto           read = [&](uint32_t n) {
        std::vector<uint8_t> b(n);
        pvr_texture_read(base, b.data(), n);
        return b;
    };
    if (fmt <= 2) {
        const auto f = nr::Texel16(fmt);
        if (vq) {
            const auto d = read(2048 + (mip ? pvr_mip_offset(fmt, true, w) : 0) + (w / 2) * (h / 2));
            return nr::convert_vq(d, w, h, f, mip, 256, argb);
        }
        if (mip) {
            const auto                  d = read(pvr_mip_offset(fmt, false, w) + 2 * w * w);
            std::vector<nr::MipLevel>   levels;
            if (!nr::convert16_mips(d, w, f, levels) || levels.empty()) return false;
            argb = std::move(levels[0].argb);
            return true;
        }
        const uint32_t rowW = linear && strideSel && k.stride ? k.stride : w;  // linear com stride select
        const auto     d    = read(2 * (linear ? rowW * h : w * h));
        std::vector<uint16_t> px(size_t(w) * h);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const size_t from = linear ? size_t(y) * rowW + x : size_t(y) * w + x;  // twiddled: o vetor inteiro
                std::memcpy(&px[size_t(y) * w + x], d.data() + 2 * from, 2);
            }
        return nr::convert16(px, w, h, f, !linear, argb);
    }
    if (fmt == 5 && !mip) {  // paleta de 4 bits: o banco do seletor (TCW bits 26-21)
        std::vector<uint32_t> pal(16);
        const uint32_t        sel = (k.tcw >> 21) & 0x3F;
        for (uint32_t i = 0; i < 16; ++i) pal[i] = pvr_pal_entry(sel * 16 + i);
        return nr::convert_pal4(read(w * h / 2), w, h, pal, argb);
    }
    return false;  // paleta de 8 bits, YUV, bump: sem conversao nativa (o jogo nao os usa nos arquivos)
}

bool content_id(const TextureKey& k, nr::TextureId& id) {
    std::vector<uint32_t> argb;
    if (!convert_texture(k, argb)) return false;
    id = nr::texture_id(k.width, k.height, argb);
    return true;
}

void install(Mode m, uint32_t every) {
    if (m == Mode::Off) {
        pvr_render_set_capture(nullptr);
        return;
    }
    g_cap        = std::make_unique<Capture>();  // estatisticas, quadro e conjuntos de ids recomecam
    g_cap->mode  = m;
    g_cap->every = every ? every : 1;
    pvr_render_set_capture(g_cap.get());
}

const Frame& last_frame() {
    static const Frame empty;
    return g_cap ? g_cap->frame : empty;
}

const Stats& stats() {
    static const Stats empty;
    return g_cap ? g_cap->st : empty;
}

std::string summary() {
    if (!g_cap) return {};
    const Stats& s = g_cap->st;
    char         b[1024];
    std::snprintf(b, sizeof b,
                  "[render-ir] renders %llu (conferidos %llu): desenhos %llu, vertices %llu, triangulos %llu, sprites %llu; "
                  "IR invalida %llu, triangulos divergentes %llu, imagens divergentes %llu; texturas conferidas %llu "
                  "(divergentes %llu, sem conversao nativa %llu; chaves distintas %llu, conteudos distintos %llu); quadros "
                  "divergentes %llu; volumes modificadores %llu (%llu triangulos, sem fechamento %llu), parametros "
                  "omitidos %llu%s%s\n",
                  (unsigned long long)s.renders, (unsigned long long)s.checked, (unsigned long long)s.draws,
                  (unsigned long long)s.vertices, (unsigned long long)s.triangles, (unsigned long long)s.sprites,
                  (unsigned long long)s.invalid, (unsigned long long)s.triangle_mismatch, (unsigned long long)s.image_mismatch,
                  (unsigned long long)s.textures_checked, (unsigned long long)s.texture_mismatch,
                  (unsigned long long)s.textures_without_native, (unsigned long long)s.distinct_texture_keys,
                  (unsigned long long)s.distinct_texture_ids, (unsigned long long)s.frame_mismatch,
                  (unsigned long long)s.modifier_volumes, (unsigned long long)s.modifier_params,
                  (unsigned long long)s.modifier_unclosed, (unsigned long long)s.skipped_params,
                  s.first_error.empty() ? "" : "; primeira: ", s.first_error.c_str());
    return b;
}

} // namespace bs::transitional::render_capture
