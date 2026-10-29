#include "render/kamui/list_sink.h"

#include "render/soft/soft_device.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace bs::native::render::kamui {
namespace {

// PCW bits 31-29
enum : uint32_t { kEndOfList = 0, kUserClip = 1, kObjListSet = 2, kGlobal = 4, kSprite = 5, kVertex = 7 };

bool poly_global64(uint32_t pcw) {
    const uint32_t col = (pcw >> 4) & 3, tex = (pcw >> 3) & 1, off = (pcw >> 2) & 1, vol = (pcw >> 6) & 1;
    return vol ? col == 2 : (col == 2 && tex && off);
}

bool poly_vertex64(uint32_t pcw) {
    const uint32_t col = (pcw >> 4) & 3, tex = (pcw >> 3) & 1, vol = (pcw >> 6) & 1;
    if (!tex) return false;
    return vol ? true : col == 1;  // textura + dois volumes: 11-14; textura + cor float: 5/6
}

float f32(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}
float f16hi(uint32_t h) { return f32(h << 16); }

using Color = soft::Color;
using Vtx   = soft::Vertex;
using Prim  = soft::Tri;

Color unpack(uint32_t c) {
    return {((c >> 24) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f};
}
// cor em float do TA: saturada em [0, 1] e quantizada em 8 bits (a regra do decodificador do legado)
float q8(float v) {
    if (!(v > 0)) return 0;
    if (v >= 1) return 1;
    return std::round(v * 255.f) / 255.f;
}
Color fcolor(const uint32_t* w) { return {q8(f32(w[0])), q8(f32(w[1])), q8(f32(w[2])), q8(f32(w[3]))}; }

ListType list_type(uint32_t l) {
    switch (l) {
    case 1: return ListType::OpaqueModifier;
    case 2: return ListType::Translucent;
    case 3: return ListType::TranslucentModifier;
    case 4: return ListType::PunchThrough;
    default: return ListType::Opaque;
    }
}

// O decodificador: o Parser do legado (estado do global, recorte do usuario, strips, sprites) e a montagem da IR da
// captura (desenhos por strip, triangulo a triangulo quando o estado muda no meio, volumes pela instrucao do global)
class Decoder {
public:
    Decoder(const FrameParams& frame, uint32_t stride, ListFrame& out) : out_(out), stride_(stride) {
        out_ = {};
        out_.cl.frame        = frame;
        out_.texture_stride  = stride;
    }

    void feed(const ListParam& p) {
        const uint32_t pcw = p.w[0], type = pcw >> 29;
        if (type == kEndOfList) {
            strip_break();
            return;
        }
        if (type == kUserClip) {  // w4..w7: x/y minimo e maximo, em tiles de 32 pixels
            cx0_ = int(p.w[4] & 0x3F) * 32, cy0_ = int(p.w[5] & 0xF) * 32;
            cx1_ = int(p.w[6] & 0x3F) * 32 + 31, cy1_ = int(p.w[7] & 0xF) * 32 + 31;
            return;
        }
        if (type == kObjListSet) return;
        if (type == kGlobal || type == kSprite) {
            capture_global(p);
            global(p, type == kSprite);
            return;
        }
        if (type != kVertex) return;
        list_ = p.list;
        if (!g_.supported || p.list == 1 || p.list == 3) {
            ++out_.skipped;
            if (p.list != 1 && p.list != 3) ++out_.unsupported;
            if (p.list == 1 || p.list == 3) modifier(p);
            else ++out_.skipped_params;
            if (pcw & (1u << 28)) strip_break();
            return;
        }
        if (g_.sprite) sprite(p);
        else vertex(p);
    }

    void finish() { out_.modifier_unclosed = out_.modifier_volumes - mod_closed_; }

private:
    struct Global {
        uint32_t pcw = 0, isp = 0, tsp = 0, tcw = 0;
        Color    face, face_off;
        uint32_t sprite_base = 0, sprite_off = 0;
        bool     sprite = false, supported = true;
    };

    ListFrame& out_;
    uint32_t   stride_;
    Global     g_;
    uint32_t   list_ = 0;
    int        cx0_ = 0, cy0_ = 0, cx1_ = 0, cy1_ = 0;
    // strips: os vertices do strip atual (o Parser e a captura guardavam os mesmos)
    std::vector<Vtx> strip_;
    int32_t          open_ = -1;  // desenho aberto pelo strip atual
    bool             tri_mode_ = false;
    // volumes modificadores
    bool     mod_open_ = false, mod_closing_ = false, mod_cur_closed_ = false, mod_last_ = false;
    uint32_t mod_instr_ = 0, mod_closed_ = 0;
    uint8_t  mod_cull_ = 0;

    void strip_break() {
        strip_.clear();
        open_ = -1, tri_mode_ = false;
    }

    // o lado da captura: um global de volume com a instrucao 1/2 fecha o volume nos triangulos seguintes; o proximo
    // global (de qualquer lista) comeca outro
    void capture_global(const ListParam& g) {
        if (mod_closing_) mod_open_ = false;
        mod_closing_ = false;
        if (g.list != 1 && g.list != 3) {
            if (g.w[0] & 0x40) ++out_.global_two_volume;
            return;
        }
        mod_instr_   = (g.w[1] >> 29) & 7;
        mod_closing_ = mod_instr_ == 1 || mod_instr_ == 2;
        mod_cull_    = uint8_t((g.w[1] >> 27) & 3);
        mod_last_    = g.w[0] & 0x40;  // PCW bit 6: fechamento por inversao; sem ele, por OU
        if (mod_instr_ > 2) ++out_.volume_bad_instruction;
    }

    void global(const ListParam& p, bool sprite) {
        g_.pcw    = p.w[0];
        g_.isp    = p.w[1];
        g_.tsp    = p.w[2];
        g_.tcw    = p.w[3];
        g_.sprite = sprite;
        strip_break();
        const uint32_t obj = g_.pcw & 0xFF, col = (obj >> 4) & 3;
        g_.supported       = true;
        if (p.list == 1 || p.list == 3) return;  // volume modificador
        if (obj & 0x40) {
            ++out_.two_volume;
            g_.supported = false;
        }
        if (sprite) {
            g_.sprite_base = p.w[4];
            g_.sprite_off  = p.w[5];
        } else if (col == 2) {  // intensidade 1: cor de face no global
            if (p.words == 16) {
                g_.face     = fcolor(&p.w[8]);
                g_.face_off = fcolor(&p.w[12]);
            } else {
                g_.face = fcolor(&p.w[4]);
            }
        }  // col == 3 (intensidade 2): mantem a cor de face anterior
        if ((obj & 8) && !texture_supported(g_.tcw)) g_.supported = false;
    }

    Color intensity(const Color& face, float i) const { return {face.a, q8(face.r * i), q8(face.g * i), q8(face.b * i)}; }

    Prim base() const {
        Prim t;
        t.list     = list_;
        t.isp      = g_.isp;
        t.tsp      = g_.tsp;
        t.tcw      = g_.tcw;
        t.tex      = g_.pcw & 8;
        t.offset   = g_.pcw & 4;
        t.gouraud  = g_.pcw & 2;
        t.shadow   = g_.pcw & 0x80;
        t.clipMode = (g_.pcw >> 16) & 3;
        t.cx0 = cx0_, t.cy0 = cy0_, t.cx1 = cx1_, t.cy1 = cy1_;
        return t;
    }

    DrawState state_of(const Prim& t) {
        uint32_t handle = 0;
        if (t.tex) {
            const TextureKey k{t.tcw, 8u << ((t.tsp >> 3) & 7), 8u << (t.tsp & 7), stride_};
            size_t           i = 0;
            while (i < out_.textures.size() && !(out_.textures[i] == k)) ++i;
            if (i == out_.textures.size()) out_.textures.push_back(k);
            handle = uint32_t(i + 1);
        }
        return draw_state(t, handle);
    }

    void vertex(const ListParam& p) {
        const uint32_t obj = g_.pcw & 0xFF, col = (obj >> 4) & 3;
        const bool     tex = obj & 8, uv16 = obj & 1;
        Vtx            v;
        v.x = f32(p.w[1]), v.y = f32(p.w[2]), v.z = f32(p.w[3]);
        if (tex) {
            if (uv16) v.u = f16hi(p.w[4] >> 16), v.v = f16hi(p.w[4] & 0xFFFF);
            else v.u = f32(p.w[4]), v.v = f32(p.w[5]);
        }
        switch (col) {
        case 0:  // empacotada
            v.col = unpack(p.w[6]);
            if (tex) v.off = unpack(p.w[7]);
            break;
        case 1:  // float (sem textura: w4..w7; com textura: 64 bytes, w8..w15)
            if (tex) {
                v.col = fcolor(&p.w[8]);
                v.off = fcolor(&p.w[12]);
            } else {
                v.col = fcolor(&p.w[4]);
            }
            break;
        default:  // intensidade 1 ou 2
            v.col = intensity(g_.face, f32(p.w[6]));
            if (tex) v.off = intensity(g_.face_off, f32(p.w[7]));
            break;
        }
        strip_.push_back(v);
        const size_t n = strip_.size();
        if (n >= 3) {
            ++out_.triangles;
            const DrawState s          = state_of(base());
            CommandList&    cl         = out_.cl;
            const bool      contiguous = open_ >= 0 && cl.draws[size_t(open_)].first_vertex +
                                                          cl.draws[size_t(open_)].vertex_count ==
                                                      cl.vertices.size();
            if (!tri_mode_ && contiguous && s == cl.draws[size_t(open_)].state) {
                cl.draws[size_t(open_)].vertex_count++;  // o strip continua com o mesmo estado
                cl.vertices.push_back(ir_vertex(v));
            } else if (!tri_mode_ && n == 3) {  // o primeiro triangulo do strip abre o desenho
                open_ = int32_t(cl.draws.size());
                cl.draws.push_back({s, uint32_t(cl.vertices.size()), 3});
                for (size_t i = 0; i < 3; ++i) cl.vertices.push_back(ir_vertex(strip_[i]));
            } else {
                // estado mudou no meio do strip (recorte do usuario entre vertices): daqui ate o fim do strip, um
                // desenho por triangulo, na ordem exata do decodificador (n par: os dois primeiros trocados)
                tri_mode_ = true, open_ = -1;
                Vtx t[3] = {strip_[n - 3], strip_[n - 2], strip_[n - 1]};
                if (n % 2 == 0) std::swap(t[0], t[1]);
                cl.draws.push_back({s, uint32_t(cl.vertices.size()), 3});
                for (const Vtx& x : t) cl.vertices.push_back(ir_vertex(x));
            }
        }
        if (p.w[0] & (1u << 28)) strip_break();  // fim de strip
    }

    void sprite(const ListParam& p) {
        Vtx a, b, cc, d;
        a.x = f32(p.w[1]), a.y = f32(p.w[2]), a.z = f32(p.w[3]);
        b.x = f32(p.w[4]), b.y = f32(p.w[5]), b.z = f32(p.w[6]);
        cc.x = f32(p.w[7]), cc.y = f32(p.w[8]), cc.z = f32(p.w[9]);
        d.x = f32(p.w[10]), d.y = f32(p.w[11]);
        if (g_.pcw & 8) {
            a.u = f16hi(p.w[13] >> 16), a.v = f16hi(p.w[13] & 0xFFFF);
            b.u = f16hi(p.w[14] >> 16), b.v = f16hi(p.w[14] & 0xFFFF);
            cc.u = f16hi(p.w[15] >> 16), cc.v = f16hi(p.w[15] & 0xFFFF);
        }
        // D nao traz z nem UV: o hardware os tira das equacoes de superficie de A, B, C; z (= 1/w) e linear na tela,
        // u e v com perspectiva (planos de u*z e v*z, divididos por z em D)
        const float det   = (b.x - a.x) * (cc.y - a.y) - (cc.x - a.x) * (b.y - a.y);
        auto        plane = [&](float fa, float fb, float fc) {
            if (det == 0) return fa;
            const float s = ((d.x - a.x) * (cc.y - a.y) - (cc.x - a.x) * (d.y - a.y)) / det;
            const float t = ((b.x - a.x) * (d.y - a.y) - (d.x - a.x) * (b.y - a.y)) / det;
            return fa + s * (fb - fa) + t * (fc - fa);
        };
        d.z = plane(a.z, b.z, cc.z);
        if ((g_.pcw & 8) && d.z != 0) {
            d.u = plane(a.u * a.z, b.u * b.z, cc.u * cc.z) / d.z;
            d.v = plane(a.v * a.z, b.v * b.z, cc.v * cc.z) / d.z;
        }
        for (Vtx* x : {&a, &b, &cc, &d}) x->col = unpack(g_.sprite_base), x->off = unpack(g_.sprite_off);
        // dois desenhos de 3 vertices, (A, B, C) e (A, C, D), na ordem do decodificador
        const DrawState s      = state_of(base());
        const Vtx*      t1[3]  = {&a, &b, &cc};
        const Vtx*      t2[3]  = {&a, &cc, &d};
        CommandList&    cl     = out_.cl;
        for (const Vtx* const* t : {t1, t2}) {
            cl.draws.push_back({s, uint32_t(cl.vertices.size()), 3});
            for (int i = 0; i < 3; ++i) cl.vertices.push_back(ir_vertex(*t[i]));
        }
        ++out_.sprites;
        open_ = -1;
    }

    // vertice de volume modificador (64 bytes): A, B, C em x, y, 1/w nas palavras 1..9
    void modifier(const ListParam& p) {
        ++out_.modifier_params;
        CommandList& cl = out_.cl;
        if (!mod_open_ || cl.modifiers.back().list != list_type(p.list)) {
            cl.modifiers.push_back({uint32_t(cl.vertices.size()), 0, true, list_type(p.list)});
            cl.modifiers.back().cull = mod_cull_;
            ++out_.modifier_volumes;
            mod_open_ = true, mod_cur_closed_ = false;
        }
        ModifierVolume& mv = cl.modifiers.back();
        if (mv.cull != mod_cull_) ++out_.volume_mixed_cull;
        if (mod_closing_) {
            mv.inclusion = mod_instr_ == 1, mv.closed = true;
            if (!mod_last_ && mv.or_from == ~0u) mv.or_from = mv.triangle_count;
            if (!mod_cur_closed_) {
                mod_cur_closed_ = true, ++mod_closed_;
                ++(mv.inclusion ? out_.volume_inclusion : out_.volume_exclusion);
                out_.volume_or += mv.or_from != ~0u;
            }
        }
        for (int i = 0; i < 3; ++i) {
            Vertex v;
            v.x = f32(p.w[1 + 3 * i]), v.y = f32(p.w[2 + 3 * i]), v.inv_w = f32(p.w[3 + 3 * i]);
            cl.vertices.push_back(v);
        }
        cl.modifiers.back().triangle_count++;
    }
};

} // namespace

void ListSink::list_init() {
    params_.clear();
    bytes_ = 0;
    list_ = -1, vertex64_ = false, pending_half_ = false;
}

void ListSink::list_continue() { list_ = -1; }

void ListSink::push(const uint32_t* w, uint32_t words) {
    ListParam p;
    p.list  = uint32_t(list_ < 0 ? 0 : list_);
    p.words = words;
    std::memcpy(p.w, w, words * 4);
    params_.push_back(p);
}

BlockResult ListSink::feed(const uint32_t* d) {
    BlockResult r;
    bytes_ += 32;
    if (pending_half_) {  // segunda metade de um parametro de 64 bytes
        uint32_t w[16];
        std::memcpy(w, half_.w, 32);
        std::memcpy(w + 8, d, 32);
        push(w, 16);
        pending_half_ = false;
        return r;
    }
    const uint32_t pcw = d[0], type = pcw >> 29;
    if ((type == kGlobal || type == kSprite) && list_ < 0) list_ = int((pcw >> 24) & 7);
    switch (type) {
    case kEndOfList:
        if (list_ >= 0 && list_ <= 4) r.list_end = list_;
        push(d, 8);
        list_ = -1;
        return r;
    case kUserClip:
    case kObjListSet:
        push(d, 8);
        return r;
    case kGlobal: {
        const bool modvol = list_ == 1 || list_ == 3;
        vertex64_         = modvol || poly_vertex64(pcw);
        if (!modvol && poly_global64(pcw)) break;  // 64 bytes: espera a segunda metade
        push(d, 8);
        return r;
    }
    case kSprite:
        vertex64_ = true;
        push(d, 8);
        return r;
    case kVertex:
        if (vertex64_) break;
        push(d, 8);
        return r;
    default:
        r.invalid = true;
        return r;
    }
    std::memcpy(half_.w, d, 32);
    pending_half_ = true;
    return r;
}

void ListSink::build(const FrameParams& frame, uint32_t texture_stride, ListFrame& out) const {
    Decoder dec(frame, texture_stride, out);
    for (const ListParam& p : params_) dec.feed(p);
    dec.finish();
}

DrawState draw_state(const soft::Tri& t, uint32_t texture_handle) {
    DrawState s;
    s.list          = list_type(t.list);
    s.texture.value = t.tex ? texture_handle : 0;
    s.src           = Blend(t.tsp >> 29);
    s.dst           = Blend((t.tsp >> 26) & 7);
    s.depth         = DepthTest(t.isp >> 29);
    s.depth_write   = !((t.isp >> 26) & 1);
    s.cull          = Cull((t.isp >> 27) & 3);
    s.filter        = Filter((t.tsp >> 13) & 3);
    s.wrap_u = ((t.tsp >> 16) & 1) ? Wrap::Clamp : (((t.tsp >> 18) & 1) ? Wrap::Mirror : Wrap::Repeat);
    s.wrap_v = ((t.tsp >> 15) & 1) ? Wrap::Clamp : (((t.tsp >> 17) & 1) ? Wrap::Mirror : Wrap::Repeat);
    static constexpr TexEnv kEnv[4] = {TexEnv::Replace, TexEnv::Modulate, TexEnv::Decal,
                                       TexEnv::ModulateAlpha};  // decal, modulate, decal alpha, modulate alpha
    s.env              = kEnv[(t.tsp >> 6) & 3];
    s.fog              = Fog((t.tsp >> 22) & 3);
    s.gouraud          = t.gouraud;
    s.use_alpha        = (t.tsp >> 20) & 1;
    s.offset_color     = t.offset;
    s.ignore_tex_alpha = (t.tsp >> 19) & 1;
    s.color_clamp      = (t.tsp >> 21) & 1;
    s.shadow           = t.shadow;
    s.mip_bias         = uint8_t((t.tsp >> 8) & 0xF);
    s.supersample      = (t.tsp >> 12) & 1;
    s.src_select       = (t.tsp >> 25) & 1;
    s.dst_select       = (t.tsp >> 24) & 1;
    s.clip             = t.clipMode == 2 ? Clip::Inside : (t.clipMode == 3 ? Clip::Outside : Clip::None);
    if (s.clip != Clip::None) s.clip_x0 = t.cx0, s.clip_y0 = t.cy0, s.clip_x1 = t.cx1, s.clip_y1 = t.cy1;
    return s;
}

Vertex ir_vertex(const soft::Vertex& v) {
    Vertex o;
    o.x = v.x, o.y = v.y, o.inv_w = v.z, o.u = v.u, o.v = v.v;
    o.color = soft::pack_color(v.col), o.offset = soft::pack_color(v.off);
    return o;
}

bool texture_supported(uint32_t tcw) {
    const uint32_t fmt    = (tcw >> 27) & 7;
    const bool     mipmap = tcw >> 31, vq = (tcw >> 30) & 1;
    const bool     linear = fmt <= 4 && ((tcw >> 26) & 1);
    if (vq) return fmt <= 2 && !linear;                    // VQ: 16 bits, twiddled
    if (mipmap) return (fmt <= 2 && !linear) || fmt == 6;  // nivel maior; paleta 4 bpp com mipmap ainda nao
    if (fmt == 3) return linear;                           // YUV422 linear (conversor YUV); twiddled ainda nao
    return fmt <= 2 || fmt == 5 || fmt == 6;
}

} // namespace bs::native::render::kamui
