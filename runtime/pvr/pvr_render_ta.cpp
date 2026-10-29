// N8.7: o rasterizador do caminho do TA (a referencia das comparacoes e do modo conferir), so no build de validacao
// (papel validacao): os triangulos do decodificador do TA desenhados pelo rasterizador em software (native/render/soft)
// com os texels lidos da VRAM (pvr_texel). O produto desenha pela IR (transitional/render_capture, modo Software).
#include "pvr/pvr_render.h"

#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace bs {
namespace {

// ---- N8.7: o decodificador das listas do TA do legado (movido de pvr_render.cpp sem mudanca): a referencia do ListSink
// nativo (native/render/kamui) e o produtor dos triangulos do rasterizador sobre o TA ----

void note(const char* what) {
    static std::vector<const char*> seen;
    for (const char* s : seen)
        if (s == what) return;
    seen.push_back(what);
    std::fprintf(stderr, "[render] ainda nao suportado: %s (omitido)\n", what);
}

float f32(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

using Color = PvrColor;  // os tipos do decodificador sao os publicos (pvr_render.h): a captura da IR recebe os mesmos

Color unpack(uint32_t c) {
    return {((c >> 24) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f};
}

float f16hi(uint32_t h) { return f32(h << 16); }  // 16 bits altos de um float

// Cor em float do TA (cor de vertice, cor de face, intensidade): o TA a guarda empacotada
// em 8 bits por canal, entao fica saturada em [0, 1] e quantizada (valores negativos, como
// os offsets de algumas cenas 3D do jogo, viram 0). Arredondamento para o mais proximo:
// a regra exata de conversao do hardware nao foi verificada.
float q8(float v) {
    if (!(v > 0)) return 0;  // tambem NaN
    if (v >= 1) return 1;
    return std::round(v * 255.f) / 255.f;
}
Color fcolor(const uint32_t* w) { return {q8(f32(w[0])), q8(f32(w[1])), q8(f32(w[2])), q8(f32(w[3]))}; }

using Vtx  = PvrVertex;
using Prim = PvrTri;

// Estado do parser (um global define os vertices seguintes).
struct Global {
    uint32_t pcw = 0, isp = 0, tsp = 0, tcw = 0;
    Color    face, faceOff;          // modo de intensidade
    uint32_t spriteBase = 0, spriteOff = 0;
    bool     sprite = false, supported = true;
};

class Parser {
public:
    std::vector<Prim> prims;
    // N8.7: os volumes modificadores (listas 1 e 3), agrupados pela regra da captura da IR: um global de volume com a
    // instrucao 1/2 fecha o volume nos triangulos seguintes; o proximo global (de qualquer lista) comeca outro
    std::vector<native::render::soft::Volume>    vols;
    std::vector<native::render::soft::VolumeTri> vtris;
    RenderStats*      st;
    PvrCaptureSink*   sink = nullptr;  // N7a: recebe o que este decodificador produz (nenhum segundo parser)

    void feed(const TaParam& p) {
        const uint32_t pcw = p.w[0], type = pcw >> 29;
        if (type == 0) {  // fim de lista
            breakStrip();
            return;
        }
        if (type == 1) {  // recorte do usuario: w4..w7 = x/y minimo e maximo, em tiles de 32 pixels
            cx0_ = int(p.w[4] & 0x3F) * 32, cy0_ = int(p.w[5] & 0xF) * 32;
            cx1_ = int(p.w[6] & 0x3F) * 32 + 31, cy1_ = int(p.w[7] & 0xF) * 32 + 31;
            return;
        }
        if (type == 2) return;  // conjunto de lista de objetos: sem efeito aqui
        if (type == 4 || type == 5) {
            if (sink) sink->global(p);
            if (volClosing_) volOpen_ = false;
            volClosing_ = false;
            if (p.list == 1 || p.list == 3) {
                volInstr_   = (p.w[1] >> 29) & 7;
                volClosing_ = volInstr_ == 1 || volInstr_ == 2;
                volCull_    = uint8_t((p.w[1] >> 27) & 3);
                volLast_    = p.w[0] & 0x40;  // PCW bit 6: fechamento por inversao (sem ele, por OU)
            }
            global(p, type == 5);
            return;
        }
        if (type != 7) return;
        list_ = p.list;
        if (!g_.supported || p.list == 1 || p.list == 3) {
            ++st->skipped;
            if (p.list != 1 && p.list != 3) ++st->unsupported;  // N8.7: um vertice que o PVR desenharia
            if (sink) (p.list == 1 || p.list == 3) ? sink->modifier(gp_, p) : sink->skipped(p);
            if (p.list == 1 || p.list == 3) volume(p);
            if (pcw & (1u << 28)) breakStrip();
            return;
        }
        if (g_.sprite) sprite(p);
        else vertex(p);
    }

private:
    Global           g_;
    TaParam          gp_;  // o ultimo global, como chegou (os volumes modificadores tem o modo nele)
    std::vector<Vtx> strip_;
    uint32_t         list_ = 0;
    int              cx0_ = 0, cy0_ = 0, cx1_ = 0, cy1_ = 0;

    bool     volOpen_ = false, volClosing_ = false, volLast_ = false;
    uint32_t volInstr_ = 0;
    uint8_t  volCull_  = 0;

    // um triangulo de volume (64 bytes): A, B, C em x, y, 1/w nas palavras 1..9
    void volume(const TaParam& p) {
        if (!volOpen_ || vols.back().list != p.list) {
            native::render::soft::Volume v;
            v.first = uint32_t(vtris.size()), v.list = p.list, v.cull = volCull_;
            vols.push_back(v);
            volOpen_ = true;
        }
        native::render::soft::Volume& v = vols.back();
        if (volClosing_) {
            v.inclusion = volInstr_ == 1, v.closed = true;
            if (!volLast_ && v.or_from == ~0u) v.or_from = v.count;
        }
        native::render::soft::VolumeTri t;
        for (int i = 0; i < 3; ++i) t.x[i] = f32(p.w[1 + 3 * i]), t.y[i] = f32(p.w[2 + 3 * i]), t.z[i] = f32(p.w[3 + 3 * i]);
        vtris.push_back(t);
        ++v.count;
    }

    void breakStrip() {
        strip_.clear();
        if (sink) sink->strip_break();
    }

    void global(const TaParam& p, bool sprite) {
        gp_       = p;
        g_.pcw    = p.w[0];
        g_.isp    = p.w[1];
        g_.tsp    = p.w[2];
        g_.tcw    = p.w[3];
        g_.sprite = sprite;
        breakStrip();
        const uint32_t obj = g_.pcw & 0xFF, col = (obj >> 4) & 3;
        g_.supported       = true;
        if (p.list == 1 || p.list == 3) return;  // volume modificador
        if (obj & 0x40) {  // N8.7: contado (o jogo nao usa; a IR nao tem o segundo conjunto de parametros)
            note("poligono com dois volumes");
            ++st->two_volume;
            g_.supported = false;
        }
        if (sprite) {
            g_.spriteBase = p.w[4];
            g_.spriteOff  = p.w[5];
        } else if (col == 2) {  // intensidade 1: cor de face no global
            if (p.words == 16) {
                g_.face    = fcolor(&p.w[8]);
                g_.faceOff = fcolor(&p.w[12]);
            } else {
                g_.face = fcolor(&p.w[4]);
            }
        }  // col == 3 (intensidade 2): mantem a cor de face anterior
        if ((obj & 8) && ((g_.tcw >> 27) & 7) == 3 && std::getenv("BS_YUV_DEBUG")) {  // diagnostico do video
            static std::set<uint64_t> seenYuv;
            if (seenYuv.insert((uint64_t(g_.tcw) << 32) | g_.tsp).second)
                std::fprintf(stderr, "[yuv] poligono TCW %08X TSP %08X (u %u v %u) stride %u\n", g_.tcw, g_.tsp, 8u << ((g_.tsp >> 3) & 7), 8u << (g_.tsp & 7), pvr_reg(0x005F80E4) & 31);
        }
        if ((obj & 8) && !pvr_texture_supported({g_.tcw})) {
            // um aviso por combinacao (mipmap, VQ, formato, linear), para saber o que falta
            static std::set<uint32_t> seen;
            const uint32_t            kind = g_.tcw & 0xFC000000;
            if (seen.insert(kind).second)
                std::fprintf(stderr, "[render] textura nao suportada (omitida): TCW %08X (mipmap %u, VQ %u, formato %u, linear %u)\n",
                             g_.tcw, g_.tcw >> 31, (g_.tcw >> 30) & 1, (g_.tcw >> 27) & 7, (g_.tcw >> 26) & 1);
            note("formato/modo de textura (mipmap, VQ, YUV twiddled ou bump)");
            g_.supported = false;
        }
    }

    Color intensity(const Color& face, float i) const {
        return {face.a, q8(face.r * i), q8(face.g * i), q8(face.b * i)};
    }

    void vertex(const TaParam& p) {
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
            if (tex) v.off = intensity(g_.faceOff, f32(p.w[7]));
            break;
        }
        strip_.push_back(v);
        if (sink) sink->vertex(base(), v, (p.w[0] >> 28) & 1);
        if (strip_.size() >= 3) {
            Prim         t = base();
            const size_t n = strip_.size();
            t.v[0] = strip_[n - 3], t.v[1] = strip_[n - 2], t.v[2] = strip_[n - 1];
            if (n % 2 == 0) std::swap(t.v[0], t.v[1]);  // strips alternam o sentido
            prims.push_back(t);
            ++st->triangles;
        }
        if (p.w[0] & (1u << 28)) strip_.clear();  // fim de strip (a captura ja recebeu o fim no vertice)
    }

    void sprite(const TaParam& p) {
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
        // D nao traz z nem UV: o hardware os tira das equacoes de superficie de A, B, C
        // (manual do Dreamcast, 3.1.1.1; revisao Codex 2026-10-14). z (= 1/w) e linear na
        // tela; u e v com perspectiva: planos de u*z e v*z, divididos por z em D.
        const float det = (b.x - a.x) * (cc.y - a.y) - (cc.x - a.x) * (b.y - a.y);
        auto plane = [&](float fa, float fb, float fc) {
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
        for (Vtx* x : {&a, &b, &cc, &d}) x->col = unpack(g_.spriteBase), x->off = unpack(g_.spriteOff);
        if (sink) sink->sprite(base(), a, b, cc, d);
        Prim t1 = base(), t2 = base();
        t1.v[0] = a, t1.v[1] = b, t1.v[2] = cc;
        t2.v[0] = a, t2.v[1] = cc, t2.v[2] = d;
        prims.push_back(t1);
        prims.push_back(t2);
        ++st->sprites;
    }

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
};

// ---- o rasterizador sobre o TA ----

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

void pvr_decode_legacy(PvrCaptureSink* sink, RenderStats& st, PvrLegacyDecode& out) {
    Parser parser;
    parser.st   = &st;
    parser.sink = sink;
    for (const auto& p : ta_params()) parser.feed(p);
    out.prims = std::move(parser.prims), out.vols = std::move(parser.vols), out.vtris = std::move(parser.vtris);
}

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
