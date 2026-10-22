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

#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "pvr/vram.h"

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

// O PVR e outro hardware: o modo de ponto flutuante do SH-4 (FPSCR: arredondamento para
// zero, FTZ/DAZ), que o runtime aplica na thread do jogo, nao vale no render. Toda conta do
// render roda com o MXCSR padrao (mais proximo, sem FTZ/DAZ), na thread que chama e em cada
// worker da resolucao paralela (cujo MXCSR o pool nao garante); o anterior e restaurado.
std::atomic<bool> g_fpViolation{false};  // algum worker calculou fora do ambiente fixo

struct RenderFpEnv {
    unsigned saved = _mm_getcsr();
    RenderFpEnv() { _mm_setcsr(0x1F80); }
    ~RenderFpEnv() { _mm_setcsr(saved); }
    RenderFpEnv(const RenderFpEnv&)            = delete;
    RenderFpEnv& operator=(const RenderFpEnv&) = delete;
};

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
            global(p, type == 5);
            return;
        }
        if (type != 7) return;
        list_ = p.list;
        if (!g_.supported || p.list == 1 || p.list == 3) {
            ++st->skipped;
            if (sink) (p.list == 1 || p.list == 3) ? sink->modifier(gp_, p) : sink->skipped(p);
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
        if (obj & 0x40) {
            note("poligono com dois volumes");
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
        t.clipMode = (g_.pcw >> 16) & 3;
        t.cx0 = cx0_, t.cy0 = cy0_, t.cx1 = cx1_, t.cy1 = cy1_;
        return t;
    }
};

class Raster {
public:
    int                   w, h;
    PvrFrame              fr;     // registradores do quadro (lidos uma vez, ou vindos da IR)
    std::vector<uint32_t> color;  // ARGB8888
    std::vector<float>    depth;  // 1/w

    Raster(int w_, int h_, const PvrFrame& f)
        : w(w_), h(h_), fr(f), color(size_t(w_) * h_, 0), depth(size_t(w_) * h_, 0.f) {}

    // Fragmento ordenado por pixel (translucido em autosort ou punch-through).
    struct Frag {
        uint32_t pixel, seq, tsp;
        float    z;
        Color    c;
        bool     zwrite;
    };
    std::vector<Frag>* frags = nullptr;  // nao nulo: a lista atual e ordenada por pixel
    uint32_t           seq   = 0;

    // Autosort translucido (ISP_FEED_CFG bit 0 = 0; manual do Dreamcast 3.4.2): em cada
    // pixel, os fragmentos que passaram (>=, compare do poligono ignorado) contra a
    // profundidade das listas opacas sao misturados do mais distante (menor 1/w) para o
    // mais proximo; empate: o primeiro submetido e o mais distante. Sem z-write.
    void resolveSorted(std::vector<Frag>& f) {
        forEachPixel(f, [](const Frag& p, const Frag& q) { return p.z != q.z ? p.z < q.z : p.seq < q.seq; },
                     [&](const Frag* g, size_t n) {
                         for (size_t k = 0; k < n; ++k) color[g[k].pixel] = blend(g[k].tsp, g[k].c, unpack(color[g[k].pixel]));
                     });
        f.clear();
    }

    // Agrupa os fragmentos por pixel (ordenacao por contagem, linear no numero de
    // fragmentos) e ordena cada grupo por `less`, uma ordem total: (z, ordem de submissao),
    // e um poligono nao gera dois fragmentos no mesmo pixel. Mesmo resultado de uma
    // ordenacao global, sem o custo dela com camadas translucidas de tela inteira.
    template <class Less, class Fn> void forEachPixel(const std::vector<Frag>& f, Less less, Fn fn) {
        if (f.empty()) return;
        const size_t          pixels = size_t(w) * h;
        std::vector<uint32_t> start(pixels + 1, 0);
        for (const Frag& g : f) ++start[g.pixel + 1];
        for (size_t i = 1; i <= pixels; ++i) start[i] += start[i - 1];
        std::vector<uint32_t> next(start.begin(), start.end() - 1);
        sorted_.resize(f.size());
        for (const Frag& g : f) sorted_[next[g.pixel]++] = g;
        // cada pixel e independente (fatia propria de sorted_, saida no proprio pixel): as
        // linhas sao resolvidas em paralelo, com o mesmo resultado da ordem sequencial
        std::vector<int> rows(static_cast<size_t>(h));
        for (int y = 0; y < h; ++y) rows[size_t(y)] = y;
        // diagnostico: BS_RENDER_SEQ=1 resolve as linhas em sequencia (mesmo resultado; isola efeitos das threads)
        static const bool seqOnly = std::getenv("BS_RENDER_SEQ") != nullptr;
        auto              body    = [&](int y) {
            const RenderFpEnv fpEnv;  // MXCSR do worker e indefinido (revisao Codex 2026-10-14)
            if ((_mm_getcsr() & ~0x3Fu) != 0x1F80) g_fpViolation.store(true);  // ver pvr_render_take_fp_violation
            for (size_t p = size_t(y) * w, end = p + size_t(w); p < end; ++p) {
                const uint32_t b = start[p], e = start[p + 1];
                if (b == e) continue;
                if (e - b > 1) std::sort(sorted_.begin() + b, sorted_.begin() + e, less);
                fn(&sorted_[b], size_t(e - b));
            }
        };
        if (seqOnly) std::for_each(rows.begin(), rows.end(), body);
        else std::for_each(std::execution::par, rows.begin(), rows.end(), body);
    }
    std::vector<Frag> sorted_;

    // Punch-through (manual 3.4.3): ordenado por pixel da frente para tras, compare forcado
    // em >=; vale o fragmento mais proximo que passou no teste de alfa (os que falham ja
    // foram descartados e revelam a camada seguinte). Empate: o ultimo submetido, como no
    // >= de um z-buffer comum (a regra exata de empate nao foi verificada).
    void resolvePunchThrough(std::vector<Frag>& f) {
        forEachPixel(f, [](const Frag& p, const Frag& q) { return p.z != q.z ? p.z > q.z : p.seq > q.seq; },
                     [&](const Frag* g, size_t) {
                         color[g->pixel] = pack(g->c);
                         // z-write disable e ignorado no punch-through (evidencia de
                         // compatibilidade: DEmul, Bust-A-Move 4; revisao Codex 2026-10-14)
                         depth[g->pixel] = g->z;
                     });
        f.clear();
    }

    void draw(const Prim& t, uint32_t list) {
        ++seq;
        lastTex_ = nullptr;  // o TexInfo local de cada draw pode reutilizar o mesmo endereco
        const Vtx &a = t.v[0], &b = t.v[1], &c = t.v[2];
        const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (area == 0 || !std::isfinite(area)) return;
        const uint32_t cull = (t.isp >> 27) & 3;
        if (cull == 1 && std::fabs(area) < 2 * fr.cull_val) return;  // FPU_CULL_VAL
        if (cull == 2 && area < 0) return;
        if (cull == 3 && area > 0) return;
        int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
        int x1 = std::min(w - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
        int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
        int y1 = std::min(h - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
        if (t.clipMode == 2) {  // so dentro do recorte
            x0 = std::max(x0, t.cx0), x1 = std::min(x1, t.cx1);
            y0 = std::max(y0, t.cy0), y1 = std::min(y1, t.cy1);
        }
        if (x0 > x1 || y0 > y1) return;
        const float inv = 1.f / area;
        TexInfo     tex;
        if (t.tex) {
            tex.tcw    = t.tcw;
            tex.width  = 8u << ((t.tsp >> 3) & 7);
            tex.height = 8u << (t.tsp & 7);
            tex.stride = fr.tex_stride;  // TEXT_CONTROL
        }
        // listas ordenadas por pixel (translucida em autosort, punch-through): compare
        // forcado em >= (manual 3.4.2 e 3.4.3; revisao Codex 2026-10-14)
        const uint32_t cmp    = frags ? 6u : (t.isp >> 29);
        const bool     zwrite = !((t.isp >> 26) & 1);
        const uint8_t  ptRef  = uint8_t(fr.pt_ref & 0xFF);
        // HALF_OFFSET (revisao Codex 2026-10-14): bit 0 amostra do FPU (cobertura e
        // profundidade), bit 1 amostra do TSP (atributos), bit 2 meio texel; 1 = centro do
        // pixel/texel, 0 = canto superior esquerdo. O jogo escreve 7.
        const uint32_t half   = fr.half_offset;
        const float    fpuOff = (half & 1) ? 0.5f : 0.f, tspOff = (half & 2) ? 0.5f : 0.f;
        const bool     texelHalf = half & 4;
        // color clamp do TSP (bit 21): limites ARGB de FPU_CLAMP_MAX (0x0BC) e MIN (0x0C0)
        const bool  clampOn = (t.tsp >> 21) & 1;
        const Color cmax = unpack(fr.clamp_max), cmin = unpack(fr.clamp_min);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                if (t.clipMode == 3 && x >= t.cx0 && x <= t.cx1 && y >= t.cy0 && y <= t.cy1) continue;  // so fora
                const float px = x + fpuOff, py = y + fpuOff;
                float       w0 = (b.x - px) * (c.y - py) - (c.x - px) * (b.y - py);
                float       w1 = (c.x - px) * (a.y - py) - (a.x - px) * (c.y - py);
                float       w2 = (a.x - px) * (b.y - py) - (b.x - px) * (a.y - py);
                if (area < 0) w0 = -w0, w1 = -w1, w2 = -w2;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                if ((w0 == 0 && !topLeft(b, c, area)) || (w1 == 0 && !topLeft(c, a, area)) ||
                    (w2 == 0 && !topLeft(a, b, area)))
                    continue;
                const float  l0 = w0 * std::fabs(inv), l1 = w1 * std::fabs(inv), l2 = w2 * std::fabs(inv);
                const float  z  = l0 * a.z + l1 * b.z + l2 * c.z;
                const size_t i  = size_t(y) * w + x;
                if (!depthPass(cmp, z, depth[i])) continue;
                // atributos na amostra do TSP (igual a do FPU quando os bits 0 e 1 coincidem)
                float m0 = l0, m1 = l1, m2 = l2;
                if (tspOff != fpuOff) {
                    const float qx = x + tspOff, qy = y + tspOff;
                    m0 = ((b.x - qx) * (c.y - qy) - (c.x - qx) * (b.y - qy)) * inv;
                    m1 = ((c.x - qx) * (a.y - qy) - (a.x - qx) * (c.y - qy)) * inv;
                    m2 = 1.f - m0 - m1;
                }
                const float zt = m0 * a.z + m1 * b.z + m2 * c.z;
                // correcao de perspectiva: atributos * (1/w) interpolados, / (1/w)
                const float p0 = m0 * a.z / zt, p1 = m1 * b.z / zt, p2 = m2 * c.z / zt;
                auto        lerp = [&](float va, float vb, float vc) { return p0 * va + p1 * vb + p2 * vc; };
                const Vtx&  fl   = c;  // sombreamento plano: cor do ultimo vertice
                Color col = t.gouraud ? Color{lerp(a.col.a, b.col.a, c.col.a), lerp(a.col.r, b.col.r, c.col.r),
                                              lerp(a.col.g, b.col.g, c.col.g), lerp(a.col.b, b.col.b, c.col.b)}
                                      : fl.col;
                Color off = t.gouraud ? Color{0, lerp(a.off.r, b.off.r, c.off.r), lerp(a.off.g, b.off.g, c.off.g),
                                              lerp(a.off.b, b.off.b, c.off.b)}
                                      : fl.off;
                if (!((t.tsp >> 20) & 1)) col.a = 1.f;  // use alpha = 0
                Color out = col;
                if (t.tex) out = shade(t, tex, lerp(a.u, b.u, c.u), lerp(a.v, b.v, c.v), col, off, texelHalf);
                // Diagnostico: BS_RENDER_PROBE=x,y registra cada primitiva que pinta o pixel
                static const char* probe = std::getenv("BS_RENDER_PROBE");
                if (probe) {
                    int qx = -1, qy = -1;
                    std::sscanf(probe, "%d,%d", &qx, &qy);
                    if (qx == x && qy == y)
                        std::fprintf(stderr,
                                     "[probe] (%d,%d) lista %u tsp %08X tcw %08X uv %.7f %.7f (texel %.5f %.5f) "
                                     "col %.3f %.3f %.3f %.3f -> %.3f %.3f %.3f %.3f\n",
                                     x, y, list, t.tsp, t.tcw, lerp(a.u, b.u, c.u), lerp(a.v, b.v, c.v),
                                     lerp(a.u, b.u, c.u) * tex.width, lerp(a.v, b.v, c.v) * tex.height, col.a, col.r,
                                     col.g, col.b, out.a, out.r, out.g, out.b);
                }
                if (clampOn) {  // min(max()) e nao std::clamp: o jogo pode programar min > max
                    auto cl = [](float v, float lo, float hi) { return std::min(std::max(v, lo), hi); };
                    out = {cl(out.a, cmin.a, cmax.a), cl(out.r, cmin.r, cmax.r), cl(out.g, cmin.g, cmax.g),
                           cl(out.b, cmin.b, cmax.b)};
                }
                if (list == 4 && uint8_t(std::lround(clamp01(out.a) * 255)) < ptRef) continue;
                if (frags) {  // lista ordenada por pixel: resolvida depois de todos os poligonos
                    frags->push_back({uint32_t(i), seq, t.tsp, z, out, zwrite});
                    continue;
                }
                const uint32_t dst = color[i];
                color[i]           = list == 2 ? blend(t.tsp, out, unpack(dst)) : pack(out);
                if (probe) {
                    int qx = -1, qy = -1;
                    std::sscanf(probe, "%d,%d", &qx, &qy);
                    if (qx == x && qy == y) std::fprintf(stderr, "[probe]   destino %08X -> %08X\n", dst, color[i]);
                }
                if (zwrite) depth[i] = z;
            }
    }

    void fillBackground(uint32_t argb, float z) {
        std::fill(color.begin(), color.end(), argb);
        std::fill(depth.begin(), depth.end(), z);
    }

private:
    static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

    static uint32_t pack(const Color& c) {
        auto q = [](float v) { return uint32_t(std::lround(clamp01(v) * 255)); };
        return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
    }

    // aresta de cima ou da esquerda (regra de preenchimento)
    static bool topLeft(const Vtx& p, const Vtx& q, float area) {
        // y para baixo: com area > 0 as arestas vao no sentido horario na tela; a
        // aresta de cima vai para a direita (dx > 0) e as da esquerda sobem (dy < 0).
        float dx = q.x - p.x, dy = q.y - p.y;
        if (area > 0) dx = -dx, dy = -dy;
        return (dy == 0 && dx < 0) || dy > 0;
    }

    static bool depthPass(uint32_t cmp, float z, float old) {
        switch (cmp) {
        case 0: return false;
        case 1: return z < old;
        case 2: return z == old;
        case 3: return z <= old;
        case 4: return z > old;
        case 5: return z != old;
        case 6: return z >= old;
        default: return true;
        }
    }

    // Texels ja decodificados neste render (a VRAM e a paleta nao mudam durante um render):
    // preenchidos sob demanda, so os texels usados. Mesmo valor de pvr_texel.
    struct TexCache {
        std::vector<uint32_t> argb;
        std::vector<uint8_t>  have;
    };
    std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, TexCache> texCache_;
    const TexInfo* lastTex_ = nullptr;
    TexCache*      lastCache_ = nullptr;

    Color texel(const TexInfo& t, int x, int y, uint32_t tsp) {
        const int  W = int(t.width), H = int(t.height);
        const bool clampU = (tsp >> 16) & 1, clampV = (tsp >> 15) & 1;
        const bool flipU = (tsp >> 18) & 1, flipV = (tsp >> 17) & 1;
        auto       wrap  = [](int v, int n, bool cl, bool fl) {
            if (cl) return v < 0 ? 0 : (v >= n ? n - 1 : v);
            if (fl) {
                const int m = ((v % (2 * n)) + 2 * n) % (2 * n);
                return m < n ? m : 2 * n - 1 - m;
            }
            return ((v % n) + n) % n;
        };
        if (lastTex_ != &t) {
            TexCache& e = texCache_[{t.tcw, t.width, t.height, t.stride}];
            if (e.argb.empty()) {
                e.argb.resize(size_t(t.width) * t.height);
                e.have.assign(size_t(t.width) * t.height, 0);
            }
            lastTex_ = &t, lastCache_ = &e;
        }
        const uint32_t tx = uint32_t(wrap(x, W, clampU, flipU)), ty = uint32_t(wrap(y, H, clampV, flipV));
        const size_t   i  = size_t(ty) * t.width + tx;
        if (!lastCache_->have[i]) {
            lastCache_->argb[i] = pvr_texel(t, tx, ty);
            lastCache_->have[i] = 1;
        }
        return unpack(lastCache_->argb[i]);
    }

    Color shade(const Prim& t, const TexInfo& tex, float u, float v, const Color& col, const Color& off,
                bool texelHalf) {
        const float half = texelHalf ? 0.5f : 0.f;  // HALF_OFFSET bit 2: centro do texel
        const float fu = u * tex.width - half, fv = v * tex.height - half;
        Color       tx;
        if (((t.tsp >> 13) & 3) == 0) {  // ponto
            // Amostra exatamente na fronteira de texels (o texto do jogo cai assim): o texel
            // escolhido depende da precisao numerica; nao ha referencia de hardware ainda
            // (STATUS.md, comparacao com o Flycast).
            tx = texel(tex, int(std::floor(fu + 0.5f)), int(std::floor(fv + 0.5f)), t.tsp);
        } else {  // bilinear
            const int   ix = int(std::floor(fu)), iy = int(std::floor(fv));
            const float ax = fu - ix, ay = fv - iy;
            const Color c00 = texel(tex, ix, iy, t.tsp), c10 = texel(tex, ix + 1, iy, t.tsp);
            const Color c01 = texel(tex, ix, iy + 1, t.tsp), c11 = texel(tex, ix + 1, iy + 1, t.tsp);
            auto        mix = [&](float p, float q, float r, float s) {
                return (p * (1 - ax) + q * ax) * (1 - ay) + (r * (1 - ax) + s * ax) * ay;
            };
            tx = {mix(c00.a, c10.a, c01.a, c11.a), mix(c00.r, c10.r, c01.r, c11.r), mix(c00.g, c10.g, c01.g, c11.g),
                  mix(c00.b, c10.b, c01.b, c11.b)};
        }
        if ((t.tsp >> 19) & 1) tx.a = 1.f;  // ignore texture alpha
        Color o;
        switch ((t.tsp >> 6) & 3) {
        case 0: o = tx; break;                                                                // decal
        case 1: o = {tx.a, tx.r * col.r, tx.g * col.g, tx.b * col.b}; break;                   // modulate
        case 2: o = {col.a, tx.r * tx.a + col.r * (1 - tx.a), tx.g * tx.a + col.g * (1 - tx.a),
                     tx.b * tx.a + col.b * (1 - tx.a)}; break;                                 // decal alpha
        default: o = {tx.a * col.a, tx.r * col.r, tx.g * col.g, tx.b * col.b}; break;         // modulate alpha
        }
        if (t.offset) o.r += off.r, o.g += off.g, o.b += off.b;
        return o;
    }

    static float factor(uint32_t f, bool isSrc, const Color& s, const Color& d, int ch) {
        const float sc[4] = {s.a, s.r, s.g, s.b}, dc[4] = {d.a, d.r, d.g, d.b};
        switch (f) {
        case 0: return 0;
        case 1: return 1;
        case 2: return isSrc ? dc[ch] : sc[ch];  // "outra" cor
        case 3: return 1 - (isSrc ? dc[ch] : sc[ch]);
        case 4: return s.a;
        case 5: return 1 - s.a;
        case 6: return d.a;
        default: return 1 - d.a;
        }
    }

    static uint32_t blend(uint32_t tsp, const Color& s, const Color& d) {
        const uint32_t sf = tsp >> 29, df = (tsp >> 26) & 7;
        const float    sc[4] = {s.a, s.r, s.g, s.b}, dc[4] = {d.a, d.r, d.g, d.b};
        float          o[4];
        for (int ch = 0; ch < 4; ++ch) o[ch] = sc[ch] * factor(sf, true, s, d, ch) + dc[ch] * factor(df, false, s, d, ch);
        return pack({o[0], o[1], o[2], o[3]});
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

void writeBack(const Raster& r) {
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
    for (int y = std::max(0, ymin); y <= std::min(r.h - 1, ymax); ++y)
        for (int x = std::max(0, xmin); x <= std::min(r.w - 1, xmax); ++x) {
            const uint32_t c = r.color[size_t(y) * r.w + x];
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

// As listas na ordem do hardware (opaca, punch-through, translucida) e a resolucao das ordenadas por pixel; `lap`
// mede as fases (diagnostico). Devolve o numero de fragmentos translucidos ordenados.
template <class Lap> size_t drawLists(const std::vector<Prim>& prims, Raster& r, Lap lap) {
    // ISP_FEED_CFG (0x098) bit 0: 1 = pre-sort (translucidos na ordem de submissao),
    // 0 = autosort por pixel (o jogo usa 0x00800408)
    std::vector<Raster::Frag> frags;
    const bool                autosort = r.fr.autosort;
    for (uint32_t list : {0u, 4u, 2u}) {
        r.frags = (list == 4 || (list == 2 && autosort)) ? &frags : nullptr;
        for (const Prim& t : prims)
            if (t.list == list) r.draw(t, list);
        if (list == 4) r.resolvePunchThrough(frags);
        lap(list == 0 ? 1 : (list == 4 ? 2 : 3));
    }
    const size_t nfrags = frags.size();
    if (autosort) r.resolveSorted(frags);
    lap(4);
    return nfrags;
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
    return f;
}

std::vector<uint32_t> pvr_render_triangles(std::span<const PvrTri> tris, const PvrFrame& frame, uint32_t width,
                                           uint32_t height) {
    const RenderFpEnv fpEnv;
    Raster            r{int(width), int(height), frame};
    r.fillBackground(0xFF000000 | frame.background, frame.background_depth);
    drawLists(std::vector<Prim>(tris.begin(), tris.end()), r, [](int) {});
    return std::move(r.color);
}

RenderStats pvr_render(Sh4Context*) {
    const RenderFpEnv fpEnv;  // ambiente fixo nesta thread (os workers instalam o seu)
    RenderStats    st;
    const uint32_t clip = pvr_reg(R(0x13C));  // TA_GLOB_TILE_CLIP
    st.width            = ((clip & 0x3F) + 1) * 32;
    st.height           = (((clip >> 16) & 0xF) + 1) * 32;
    const PvrFrame fr = pvr_frame_from_regs();
    checkRegionSortModes(fr.autosort);
    Raster r(int(st.width), int(st.height), fr);
    r.fillBackground(0xFF000000 | fr.background, fr.background_depth);  // ISP_BACKGND_D

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
    const size_t nfrags   = replaced ? 0 : drawLists(parser.prims, r, lap);
    if (g_sink) g_sink->end(fr, parser.prims, r.color, st.width, st.height);
    writeBack(r);
    if (prof)
        std::fprintf(stderr, "[perfil] parse %.2f opaca %.2f pt %.2f transl %.2f resolve %.2f ms; fragmentos %zu\n",
                     ms[0], ms[1], ms[2], ms[3], ms[4], nfrags);
    return st;
}

bool pvr_render_take_fp_violation() { return g_fpViolation.exchange(false); }

} // namespace bs
