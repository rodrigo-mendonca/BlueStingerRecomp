// Rasterizador de referencia do PowerVR2 (software). Segue o modelo do hardware
// documentado (KallistiOS: pvr_regs, pvr_internal; Flycast so como referencia de
// comportamento, nenhum codigo copiado):
//  - entrada: parametros recebidos pelo TA desde TA_LIST_INIT (pvr/ta.cpp);
//  - buffer de 32 bits por pixel e profundidade em 1/w (o z dos vertices), do tamanho
//    de TA_GLOB_TILE_CLIP; fundo pelo plano de ISP_BACKGND_T com profundidade
//    ISP_BACKGND_D;
//  - listas na ordem opaca (0), punch-through (4), translucida (2); volumes
//    modificadores (1, 3) ainda nao;
//  - interpolacao com correcao de perspectiva (1/w), regra de preenchimento top-left,
//    amostras por HALF_OFFSET; recorte do usuario (parametro tipo 1, PCW bits 17-16);
//  - translucidos em autosort por pixel (ISP_FEED_CFG bit 0 = 0) ou na ordem de
//    submissao (pre-sort);
//  - gravacao em FB_W_SOF1 no formato de FB_W_CTRL, com FB_W_LINESTRIDE e clip.
// Aproximacoes (registradas no STATUS.md): sem fog, dither, mipmap, VQ, YUV, bump, dois
// volumes, buffers secundarios (SRC/DST select) e escala do SCALER_CTL; empates de
// amostragem exatamente na fronteira de texels dependem da precisao numerica.
#include "pvr/pvr_render.h"

#include "cpu/sh4_runtime.h"
#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "pvr/vram.h"
#include "render/soft/soft_raster.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execution>
#include <xmmintrin.h>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace bs {
namespace {

constexpr uint32_t R(uint32_t off) { return 0x005F8000 + off; }

using Color = PvrColor;  // os tipos do decodificador sao os publicos (pvr_render.h): a captura da IR recebe os mesmos

Color unpack(uint32_t c) {
    return {((c >> 24) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f};
}

float f32(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
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

PvrCaptureSink* g_sink = nullptr;  // N7a: captura da IR de render (pvr_render_set_capture)

// Estado do parser (um global define os vertices seguintes).
struct Global {
    uint32_t pcw = 0, isp = 0, tsp = 0, tcw = 0;
    Color    face, faceOff;          // modo de intensidade
    uint32_t spriteBase = 0, spriteOff = 0;
    bool     sprite = false, supported = true;
};

void note(const char* what) {
    static std::vector<const char*> seen;
    for (const char* s : seen)
        if (s == what) return;
    seen.push_back(what);
    std::fprintf(stderr, "[render] ainda nao suportado: %s (omitido)\n", what);
}

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

// Fundo: ISP/TSP/TCW e 3 vertices na memoria de parametros (caminho de 32 bits) em
// (PARAM_BASE & 0xF00000) + tag_address * 4; vertice = x, y, z, [u, v], cor base,
// [offset], com (3 + skip) palavras. M4: cor do primeiro vertice (fundo sem textura).
uint32_t backgroundColor() {
    const uint32_t t    = pvr_reg(R(0x08C));
    const uint32_t base = ((pvr_reg(R(0x020)) & 0xF00000) + ((t >> 3) & 0x1FFFFF) * 4) & (kVramSize - 1);
    auto           rd   = [](uint32_t a) {
        uint32_t v;
        std::memcpy(&v, vram_data() + (a & (kVramSize - 1)), 4);
        return v;
    };
    const uint32_t isp  = rd(base);
    const uint32_t skip = (t >> 24) & 7, vsize = (3 + skip) * 4;
    uint32_t       p    = base + 12 + (t & 7) * vsize + 12;  // pula x, y, z do 1o vertice
    if (isp & (1u << 25)) {
        note("fundo com textura");
        p += (isp & (1u << 22)) ? 4 : 8;
    }
    return rd(p);
}

// Region Array tipo 2 (FPU_PARAM_CFG bit 21): cada tile escolhe pre-sort (bit 29 da 1a
// palavra) e o ISP_FEED_CFG so vale para o tipo 1. O rasterizador nao percorre o Region
// Array; usa um modo para a tela inteira. Se algum tile pedir outro modo, avisa (uma
// vez) em vez de divergir em silencio (revisao Codex 2026-10-14). Entradas de 6 palavras,
// a ultima com o bit 31.
void checkRegionSortModes(bool autosort) {
    if (!((pvr_reg(R(0x07C)) >> 21) & 1)) return;
    uint32_t a = pvr_reg(R(0x02C)) & 0x7FFFFC;
    for (int n = 0; n < 4096; ++n, a += 24) {
        uint32_t ctrl;
        std::memcpy(&ctrl, vram_data() + (a & (kVramSize - 1)), 4);
        const bool presort = (ctrl >> 29) & 1;
        if (presort == autosort) {
            note("Region Array com modo de ordenacao por tile diferente do global");
            return;
        }
        if (ctrl >> 31) return;
    }
}

void writeBack(const native::render::soft::Image& r) {
    const uint32_t ctrl   = pvr_reg(R(0x048));
    const uint32_t sof    = pvr_reg(R(0x060)) & 0x00FFFFFC;
    const uint32_t stride = (pvr_reg(R(0x04C)) & 0x1FF) * 8;
    const uint32_t xc = pvr_reg(R(0x068)), yc = pvr_reg(R(0x06C));
    const int      xmin = int(xc & 0x7FF), xmax = int((xc >> 16) & 0x7FF);
    const int      ymin = int(yc & 0x3FF), ymax = int((yc >> 16) & 0x3FF);
    // FB_W_CTRL: bits 2-0 formato, 3 dither, 15-8 K (bit de alfa do 0555 / byte alto do
    // 0888), 23-16 limiar de alfa do 1555 (revisao Codex 2026-10-14)
    const uint32_t   fmt = ctrl & 7, kval = (ctrl >> 8) & 0xFF, thr = (ctrl >> 16) & 0xFF;
    static const int bpp[8] = {2, 2, 2, 2, 3, 4, 4, 4};
    const int w = int(r.width), h = int(r.height);
    for (int y = std::max(0, ymin); y <= std::min(h - 1, ymax); ++y)
        for (int x = std::max(0, xmin); x <= std::min(w - 1, xmax); ++x) {
            const uint32_t c = r.color[size_t(y) * w + x];
            const uint32_t a = c >> 24, rr = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            uint32_t       v = 0;
            switch (fmt) {
            case 0: v = ((kval >> 7) << 15) | ((rr >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); break;  // 0555
            case 1: v = ((rr >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); break;                       // 565
            case 2: v = ((a >> 4) << 12) | ((rr >> 4) << 8) | ((g >> 4) << 4) | (b >> 4); break;     // 4444
            case 3: v = ((a >= thr) << 15) | ((rr >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); break;   // 1555
            case 4: v = c & 0xFFFFFF; break;                                                          // 888
            case 5: v = (kval << 24) | (c & 0xFFFFFF); break;                                         // 0888
            default: v = c; break;                                                                    // 8888
            }
            const uint32_t addr = (sof + uint32_t(y) * stride + uint32_t(x) * bpp[fmt]) & (kVramSize - 1);
            std::memcpy(vram_data() + addr, &v, size_t(bpp[fmt]));
        }
}

} // namespace

void pvr_render_set_capture(PvrCaptureSink* sink) { g_sink = sink; }
uint32_t pvr_background_color() { return backgroundColor(); }

PvrFrame pvr_frame_from_regs() {
    PvrFrame f;
    f.background       = backgroundColor() & 0xFFFFFF;  // o alfa do fundo e sempre 0xFF (fillBackground)
    f.background_depth = f32(pvr_reg(R(0x088)));           // ISP_BACKGND_D
    f.cull_val         = f32(pvr_reg(R(0x078)));           // FPU_CULL_VAL
    f.tex_stride       = (pvr_reg(R(0x0E4)) & 0x1F) * 32;  // TEXT_CONTROL
    f.pt_ref           = pvr_reg(R(0x11C)) & 0xFF;         // PT_ALPHA_REF
    f.half_offset      = pvr_reg(R(0x080)) & 7;            // HALF_OFFSET
    f.clamp_max = pvr_reg(R(0x0BC)), f.clamp_min = pvr_reg(R(0x0C0));
    // ISP_FEED_CFG (0x098) bit 0: 1 = pre-sort (translucidos na ordem de submissao), 0 = autosort por pixel
    f.autosort = !(pvr_reg(R(0x098)) & 1);
    f.shadow_scale = pvr_reg(R(0x074)) & 0x1FF;  // N8.7: FPU_SHAD_SCALE
    for (size_t i = 0; i < f.fog_table.size(); ++i) f.fog_table[i] = uint16_t(pvr_reg(R(0x200 + uint32_t(i) * 4)) & 0xFFFF);
    f.fog_color   = pvr_reg(R(0x0B0)) & 0xFFFFFF;  // FOG_COL_RAM
    f.fog_density = pvr_reg(R(0x0B8)) & 0xFFFF;    // FOG_DENSITY
    return f;
}

RenderStats pvr_render(Sh4Context* c) {
    const native::render::soft::FpEnv fpEnv;  // ambiente fixo nesta thread (os workers instalam o seu)
    RenderStats    st;
    const uint32_t clip = pvr_reg(R(0x13C));  // TA_GLOB_TILE_CLIP
    st.width            = ((clip & 0x3F) + 1) * 32;
    st.height           = (((clip >> 16) & 0xF) + 1) * 32;
    const PvrFrame fr = pvr_frame_from_regs();
    checkRegionSortModes(fr.autosort);
    native::render::soft::Image r;  // o fundo pelo plano de ISP_BACKGND_T, com a profundidade de ISP_BACKGND_D
    r.width = st.width, r.height = st.height;
    r.color.assign(size_t(st.width) * st.height, 0xFF000000 | fr.background);
    r.depth.assign(size_t(st.width) * st.height, fr.background_depth);

    Parser parser;
    parser.st   = &st;
    parser.sink = g_sink;
    if (g_sink) g_sink->begin(st.width, st.height);
    for (const auto& p : ta_params()) parser.feed(p);
    // Diagnostico: BS_RENDER_PROFILE=1 mede as fases (ms) e conta fragmentos translucidos
    static const bool prof = std::getenv("BS_RENDER_PROFILE") != nullptr;
    using Clock            = std::chrono::steady_clock;
    auto   tp              = Clock::now();
    double ms[5]           = {};
    auto   lap             = [&](int i) {
        const auto n = Clock::now();
        ms[i] += std::chrono::duration<double, std::milli>(n - tp).count();
        tp = n;
    };
    lap(0);
    const bool   replaced = g_sink && g_sink->render(fr, st.width, st.height, r.color);  // N7b: --render nativo
#if !BS_VALIDACAO
    // N8.7: o produto desenha so pela IR (render_capture Software ou Native): o rasterizador sobre o TA e os texels da
    // VRAM sao da validacao. Um render que a IR nao desenhou e erro explicito (nunca um recuo silencioso)
    if (!replaced) sh4_fatal(c, "render sem o renderizador sobre a IR (o caminho do TA saiu do produto no N8.7)", c ? c->pc : 0, 0);
    const size_t nfrags = 0;
    // revisao Codex 20261028-115305: um poligono que o PVR desenharia mas a IR nao representa (bump, YUV twiddled,
    // paleta de 4 bits com mipmap, dois volumes) e erro explicito no produto, nunca um quadro publicado sem ele
    if (st.unsupported) {
        static char msg[160];
        std::snprintf(msg, sizeof msg, "render com %u vertices de estados que a IR nao representa (textura bump, YUV twiddled, "
                                       "paleta de 4 bits com mipmap ou dois volumes)", st.unsupported);
        sh4_fatal(c, msg, c ? c->pc : 0, st.unsupported);
    }
#else
    const size_t nfrags = replaced ? 0
                                   : pvr_render_ta(parser.prims, parser.vols, parser.vtris, fr, r,
                                                   [](void* l, int i) { (*static_cast<decltype(lap)*>(l))(i); }, &lap);
#endif
    if (g_sink) g_sink->end(fr, parser.prims, r.color, st.width, st.height);
    writeBack(r);
    if (prof)
        std::fprintf(stderr, "[perfil] parse %.2f opaca %.2f pt %.2f transl %.2f resolve %.2f ms; fragmentos %zu\n",
                     ms[0], ms[1], ms[2], ms[3], ms[4], nfrags);
    return st;
}

bool pvr_render_take_fp_violation() { return native::render::soft::take_fp_violation(); }

} // namespace bs
