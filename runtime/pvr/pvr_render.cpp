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
//
// N8.7 4d (docs/N8-7-KAMUI.md): so na validacao, como referencia da passagem nativa (native/render/kamui/render_pass),
// que roda em sombra em cada render: tamanho, fundo, parametros do quadro, Region Array e a gravacao no framebuffer
// (as mesmas faixas, com os mesmos bytes) sao comparados com os deste arquivo ([render-pass]). No produto, o render e
// system/render_bus.cpp.
#include "pvr/pvr_render.h"

#include "cpu/sh4_runtime.h"
#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "pvr/vram.h"
#include "render/kamui/render_pass.h"
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
#include <string>
#include <tuple>
#include <vector>

namespace bs {
namespace {

constexpr uint32_t R(uint32_t off) { return 0x005F8000 + off; }

float f32(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

PvrCaptureSink*    g_sink       = nullptr;  // N7a: captura da IR de render (pvr_render_set_capture)
const RenderStats* g_inProgress = nullptr;  // as contagens do legado durante o end() da captura

// N8.7 4d: a passagem nativa em sombra
using native::render::kamui::RenderPass;
uint64_t    g_rpRenders = 0, g_rpBytes = 0, g_rpDiffer = 0;
std::string g_rpFirst;
std::vector<std::pair<uint32_t, uint32_t>>* g_legacyTouches = nullptr;  // as faixas que writeBack marca

// negativo (BS_RENDER_PASSO_MUTACAO=formato): a sombra le FB_W_CTRL com o formato trocado (565 por 0555 e vice-versa)
bool rp_mutation() {
    static const bool m = [] {
        const char* v = std::getenv("BS_RENDER_PASSO_MUTACAO");
        return v && std::string(v) == "formato";
    }();
    return m;
}
uint32_t rp_reg(void*, uint32_t p) {
    const uint32_t v = pvr_reg(p);
    return rp_mutation() && p == 0x005F8048 ? v ^ 1 : v;
}

void rp_differ(const char* detail) {
    ++g_rpDiffer;
    if (g_rpFirst.empty()) g_rpFirst = "render " + std::to_string(g_rpRenders) + ": " + detail;
}

void note(const char* what) {
    static std::vector<const char*> seen;
    for (const char* s : seen)
        if (s == what) return;
    seen.push_back(what);
    std::fprintf(stderr, "[render] ainda nao suportado: %s (omitido)\n", what);
}


// Fundo: ISP/TSP/TCW e 3 vertices na memoria de parametros (caminho de 32 bits) em
// (PARAM_BASE & 0xF00000) + tag_address * 4; vertice = x, y, z, [u, v], cor base,
// [offset], com (3 + skip) palavras. M4: cor do primeiro vertice (fundo sem textura).
bool g_bgTextured = false;  // N8.7 4d: se o ultimo fundo lido tinha textura (a sombra compara; revisao Codex 20261029-205240)
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
    g_bgTextured        = isp & (1u << 25);
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
bool checkRegionSortModes(bool autosort) {
    if (!((pvr_reg(R(0x07C)) >> 21) & 1)) return false;
    uint32_t a = pvr_reg(R(0x02C)) & 0x7FFFFC;
    for (int n = 0; n < 4096; ++n, a += 24) {
        uint32_t ctrl;
        std::memcpy(&ctrl, vram_data() + (a & (kVramSize - 1)), 4);
        const bool presort = (ctrl >> 29) & 1;
        if (presort == autosort) {
            note("Region Array com modo de ordenacao por tile diferente do global");
            return true;
        }
        if (ctrl >> 31) return false;
    }
    return false;
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
    const int x0 = std::max(0, xmin), x1 = std::min(w - 1, xmax);
    for (int y = std::max(0, ymin); y <= std::min(h - 1, ymax); ++y) {
        for (int x = x0; x <= x1; ++x) {
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
            if (addr + uint32_t(bpp[fmt]) <= kVramSize) {
                std::memcpy(vram_data() + addr, &v, size_t(bpp[fmt]));
            } else {  // revisao Codex 20261029-013355: o pixel da a volta no fim dos 8 MB (os leitores mascaram cada byte)
                for (int i = 0; i < bpp[fmt]; ++i) vram_data()[(addr + uint32_t(i)) & (kVramSize - 1)] = uint8_t(v >> (8 * i));
            }
        }
        // N8.7 3b: marca a linha gravada na memoria das texturas (com a volta no fim, se houver)
        if (x1 >= x0) {
            const uint32_t o = (sof + uint32_t(y) * stride + uint32_t(x0) * bpp[fmt]) & (kVramSize - 1);
            const uint32_t n = uint32_t(x1 - x0 + 1) * uint32_t(bpp[fmt]);
            vram_touch(o, n);
            if (g_legacyTouches) g_legacyTouches->emplace_back(o, n);
        }
    }
}

bool same_bits(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }

// os parametros do quadro, campo a campo (os floats pelos bits: vem dos registradores e podem ser NaN)
const char* frame_params_differ(const native::render::FrameParams& a, const native::render::FrameParams& b) {
    if (a.width != b.width || a.height != b.height) return "tamanho";
    if (a.clear_color != b.clear_color || !same_bits(a.clear_depth, b.clear_depth)) return "fundo";
    if (a.fog_table != b.fog_table || a.fog_color != b.fog_color || a.fog_vertex_color != b.fog_vertex_color ||
        a.fog_density != b.fog_density)
        return "fog";
    if (a.shadow_scale != b.shadow_scale) return "sombra";
    if (!same_bits(a.punch_through_ref, b.punch_through_ref)) return "punch-through";
    if (a.translucent_autosort != b.translucent_autosort || a.sample_center != b.sample_center ||
        a.attribute_center != b.attribute_center || a.texel_center != b.texel_center)
        return "regras do quadro";
    if (!same_bits(a.small_cull, b.small_cull)) return "cull";
    if (a.clamp_min != b.clamp_min || a.clamp_max != b.clamp_max) return "clamp";
    return nullptr;
}

std::vector<std::pair<uint32_t, uint32_t>> g_nativeTouches;
void rp_touch(void*, uint32_t off, uint32_t n) { g_nativeTouches.emplace_back(off, n); }

// a gravacao nativa num armazenamento a parte; as faixas marcadas sao exatamente os bytes gravados, comparados com os
// da memoria das texturas depois da gravacao do legado
void shadow_write_back(const std::vector<uint32_t>& color, uint32_t w, uint32_t h,
                       const std::vector<std::pair<uint32_t, uint32_t>>& legacy) {
    static std::vector<uint8_t> mem(kVramSize);
    // revisao Codex 20261029-205240: cada faixa que o legado gravou comeca com o complemento do resultado, para que um
    // byte que a gravacao nativa deixe de escrever nunca passe com o valor de um render anterior
    for (const auto& [o, n] : legacy)
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t a = (o + i) & (kVramSize - 1);
            mem[a]           = uint8_t(~vram_data()[a]);
        }
    g_nativeTouches.clear();
    RenderPass::write_back(rp_reg, nullptr, color, w, h, mem.data(), rp_touch);
    if (g_nativeTouches != legacy) {
        char b[128];
        std::snprintf(b, sizeof b, "faixas gravadas: legado %zu (%08X+%u), nativo %zu (%08X+%u)", legacy.size(),
                      legacy.empty() ? 0 : legacy[0].first, legacy.empty() ? 0 : legacy[0].second,
                      g_nativeTouches.size(), g_nativeTouches.empty() ? 0 : g_nativeTouches[0].first,
                      g_nativeTouches.empty() ? 0 : g_nativeTouches[0].second);
        return rp_differ(b);
    }
    for (const auto& [o, n] : legacy) {
        g_rpBytes += n;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t a = (o + i) & (kVramSize - 1);
            if (mem[a] != vram_data()[a]) {
                char b[96];
                std::snprintf(b, sizeof b, "framebuffer em %06X: legado %02X, nativo %02X", a, vram_data()[a], mem[a]);
                return rp_differ(b);
            }
        }
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

native::render::FrameParams pvr_frame_params(uint32_t width, uint32_t height) {
    native::render::FrameParams f;
    f.width = width, f.height = height;
    f.clear_color = 0xFF000000 | backgroundColor();
    f.clear_depth = f32(pvr_reg(R(0x088)));  // ISP_BACKGND_D
    for (size_t i = 0; i < f.fog_table.size(); ++i) f.fog_table[i] = uint16_t(pvr_reg(R(0x200 + uint32_t(i) * 4)) & 0xFFFF);
    f.fog_color            = pvr_reg(R(0x0B0));            // FOG_COL_RAM
    f.fog_vertex_color     = pvr_reg(R(0x0B4));            // FOG_COL_VERT
    f.fog_density          = pvr_reg(R(0x0B8)) & 0xFFFF;   // FOG_DENSITY
    f.shadow_scale         = pvr_reg(R(0x074)) & 0x1FF;    // FPU_SHAD_SCALE
    f.punch_through_ref    = float(pvr_reg(R(0x11C)) & 0xFF) / 255.f;
    f.translucent_autosort = !(pvr_reg(R(0x098)) & 1);     // ISP_FEED_CFG bit 0
    const uint32_t half    = pvr_reg(R(0x080));            // HALF_OFFSET
    f.sample_center = half & 1, f.attribute_center = half & 2, f.texel_center = half & 4;
    f.small_cull = f32(pvr_reg(R(0x078)));  // FPU_CULL_VAL
    f.clamp_max = pvr_reg(R(0x0BC)), f.clamp_min = pvr_reg(R(0x0C0));
    return f;
}

std::string pvr_render_pass_summary() {
    return "[render-pass] renders comparados " + std::to_string(g_rpRenders) + " (" + std::to_string(g_rpBytes) +
           " bytes gravados), diferentes " + std::to_string(g_rpDiffer) +
           (g_rpFirst.empty() ? std::string() : "; primeira: " + g_rpFirst) + "\n";
}

RenderStats pvr_render(Sh4Context* c) {
    vram_heap_check();  // N8.7 3b (validacao): a memoria das texturas em sombra, as paginas tocadas desde o ultimo render
    const native::render::soft::FpEnv fpEnv;  // ambiente fixo nesta thread (os workers instalam o seu)
    RenderStats    st;
    const uint32_t clip = pvr_reg(R(0x13C));  // TA_GLOB_TILE_CLIP
    st.width            = ((clip & 0x3F) + 1) * 32;
    st.height           = (((clip >> 16) & 0xF) + 1) * 32;
    const PvrFrame fr = pvr_frame_from_regs();
    const bool     regionMismatch = checkRegionSortModes(fr.autosort);
    // N8.7 4d: a passagem nativa em sombra (antes do render: o render grava a memoria das texturas)
    ++g_rpRenders;
    {
        const RenderPass::Size       sz = RenderPass::size(rp_reg, nullptr);
        const RenderPass::Background bg = RenderPass::background(rp_reg, nullptr, vram_data());
        char                         b[128];
        if (sz.width != st.width || sz.height != st.height) {
            std::snprintf(b, sizeof b, "tamanho legado %ux%u, nativo %ux%u", st.width, st.height, sz.width, sz.height);
            rp_differ(b);
        } else if ((bg.color & 0xFFFFFF) != fr.background || bg.textured != g_bgTextured) {
            std::snprintf(b, sizeof b, "fundo legado %06X (textura %d), nativo %06X (textura %d)", fr.background,
                          int(g_bgTextured), bg.color & 0xFFFFFF, int(bg.textured));
            rp_differ(b);
        } else if (const char* f = frame_params_differ(pvr_frame_params(st.width, st.height),
                                                       RenderPass::frame_params(rp_reg, nullptr, st.width, st.height,
                                                                                bg.color))) {
            rp_differ((std::string("parametros do quadro: ") + f).c_str());
        } else if (RenderPass::region_sort_mismatch(rp_reg, nullptr, vram_data(), fr.autosort) != regionMismatch) {
            rp_differ("Region Array");
        }
    }
    native::render::soft::Image r;  // o fundo pelo plano de ISP_BACKGND_T, com a profundidade de ISP_BACKGND_D
    r.width = st.width, r.height = st.height;
    r.color.assign(size_t(st.width) * st.height, 0xFF000000 | fr.background);
    r.depth.assign(size_t(st.width) * st.height, fr.background_depth);

    // o decodificador do legado (a referencia): os triangulos do caminho do TA e a captura da IR pelos retornos
    PvrLegacyDecode dec;
    if (g_sink) g_sink->begin(st.width, st.height);
    pvr_decode_legacy(g_sink, st, dec);
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
    (void)c;
    const bool   replaced = g_sink && g_sink->render(fr, st.width, st.height, r.color);  // N7b: --render nativo
    const size_t nfrags   = replaced ? 0
                                     : pvr_render_ta(dec.prims, dec.vols, dec.vtris, fr, r,
                                                     [](void* l, int i) { (*static_cast<decltype(lap)*>(l))(i); }, &lap);
    g_inProgress = &st;
    if (g_sink) g_sink->end(fr, dec.prims, r.color, st.width, st.height);
    g_inProgress = nullptr;
    std::vector<std::pair<uint32_t, uint32_t>> touches;
    g_legacyTouches = &touches;
    writeBack(r);
    g_legacyTouches = nullptr;
    shadow_write_back(r.color, st.width, st.height, touches);
    if (prof)
        std::fprintf(stderr, "[perfil] parse %.2f opaca %.2f pt %.2f transl %.2f resolve %.2f ms; fragmentos %zu\n",
                     ms[0], ms[1], ms[2], ms[3], ms[4], nfrags);
    return st;
}

const RenderStats* pvr_render_stats_in_progress() { return g_inProgress; }

bool pvr_render_take_fp_violation() { return native::render::soft::take_fp_violation(); }

} // namespace bs
