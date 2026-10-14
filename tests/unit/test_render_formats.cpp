// Rasterizador de referencia (runtime/pvr/pvr_render.cpp): testes deterministicos de
// formatos de framebuffer, rasterizacao, cores, texturas/UV, orientacao, profundidade,
// blending e escritas na VRAM. Valores esperados calculados a mao a partir dos formatos
// documentados; o hash da cena composta (no fim) e so linha de base de regressao.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "pvr/vram.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace bs;

namespace {

uint32_t fb(float f) {
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}

constexpr uint32_t kFb = 0x100000;  // FB_W_SOF1 (caminho de 32 bits)

// Tela de 32x32 (1 tile), fundo pelo plano em PARAM_BASE 0 (tag 0, skip 1, cor `bg`),
// framebuffer em kFb no formato `fmt` com `bpp` bytes por pixel.
struct Scene {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    uint32_t             stride;  // bytes por linha

    explicit Scene(uint32_t bg, uint32_t ctrl = 1, uint32_t bpp = 2, uint32_t strideBytes = 0) {
        c.ram = ram.data();
        vram_reset();
        pvr_regs_reset();
        ta_reset();
        stride = strideBytes ? strideBytes : 32 * bpp;
        reg(0x13C, 0);
        reg(0x060, kFb);
        reg(0x04C, stride / 8);
        reg(0x048, ctrl);
        reg(0x068, 31u << 16);
        reg(0x06C, 31u << 16);
        reg(0x020, 0);
        reg(0x08C, 1u << 24);
        reg(0x088, fb(0.0001f));
        reg(0x080, 7);  // HALF_OFFSET: amostras no centro (valor escrito pelo jogo)
        const uint32_t plane[3 + 3 * 4] = {0, 0, 0, 0, 0, 1, bg, 32, 0, 1, bg, 0, 32, 1, bg};
        std::memcpy(vram_data(), plane, sizeof plane);
        reg(0x144, 0x80000000);  // TA_LIST_INIT
    }

    void reg(uint32_t off, uint32_t v) { sh4_write32(&c, 0xA05F8000 + off, v); }
    void ta(std::initializer_list<uint32_t> words) {
        uint32_t d[8] = {};
        std::copy(words.begin(), words.end(), d);
        BS_CHECK(ta_fifo_write(&c, 0x10000000, d));
    }
    void global(uint32_t pcw, uint32_t isp, uint32_t tsp, uint32_t tcw = 0) { ta({pcw, isp, tsp, tcw}); }
    // vertice de 32 bytes: x, y, z, w4..w7 (u, v, cor base, offset conforme o tipo)
    void vtx(float x, float y, float z, uint32_t w4, uint32_t w5, uint32_t w6, uint32_t w7, bool end = false) {
        ta({0xE0000000u | (end ? 1u << 28 : 0), fb(x), fb(y), fb(z), w4, w5, w6, w7});
    }
    void col(float x, float y, float z, uint32_t argb, bool end = false) { vtx(x, y, z, 0, 0, argb, 0, end); }
    void endList() { ta({0}); }
    void render() { reg(0x014, 1); }

    uint16_t px16(int x, int y) const {
        uint16_t v;
        std::memcpy(&v, vram_data() + kFb + y * stride + x * 2, 2);
        return v;
    }
    uint32_t px32(int x, int y) const {
        uint32_t v;
        std::memcpy(&v, vram_data() + kFb + y * stride + x * 4, 4);
        return v;
    }
};

constexpr uint32_t kIspAlways = 7u << 29;
constexpr uint32_t kTspOpaque = 0x20800000u;  // src ONE, dst ZERO, sem fog, sem alfa

// Textura 8x8 RGB565 twiddled em VRAM (caminho de 64 bits) 0x200000: metade esquerda
// (x < 4) vermelha, direita verde.
constexpr uint32_t kTex = 0x200000;
void loadTexture() {
    for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t x = 0; x < 8; ++x) {
            const uint16_t v = x < 4 ? 0xF800 : 0x07E0;
            const uint32_t i = pvr_twiddle(x, y, 8, 8);
            std::memcpy(vram_data() + vram_offset64(kTex + 2 * i), &v, 2);
        }
}

uint64_t fnv1a(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

} // namespace

// --- formatos de framebuffer (fundo 0x804020, alfa 0xFF) ---

BS_TEST(fb_format_565_4444_1555) {
    { Scene s(0x00804020, 1); s.render(); BS_CHECK(s.px16(5, 5) == 0x8204); }                   // 565
    { Scene s(0x00804020, 2); s.render(); BS_CHECK(s.px16(5, 5) == 0xF842); }                   // 4444
    { Scene s(0x00804020, 3 | (0xFFu << 16)); s.render(); BS_CHECK(s.px16(5, 5) == 0xC104); }   // 1555, alfa >= limiar
}

BS_TEST(fb_format_888_0888_8888) {
    {
        Scene s(0x00804020, 4, 3, 96);  // 888: 3 bytes por pixel, B G R
        s.render();
        const uint8_t* p = vram_data() + kFb + 2 * 96 + 7 * 3;
        BS_CHECK(p[0] == 0x20 && p[1] == 0x40 && p[2] == 0x80);
    }
    { Scene s(0x00804020, 5 | (0x5Au << 8), 4); s.render(); BS_CHECK(s.px32(7, 2) == 0x5A804020); }  // 0888: K no byte alto
    { Scene s(0x00804020, 6, 4); s.render(); BS_CHECK(s.px32(7, 2) == 0xFF804020); }                  // 8888
}

// 1555: o bit de alfa e alfa >= limiar (FB_W_CTRL 23-16); alfa do vertice 0x80.
BS_TEST(fb_format_1555_alpha_threshold) {
    for (uint32_t thr : {0x80u, 0x81u}) {
        Scene s(0, 3 | (thr << 16));
        s.global(0x80000002u, kIspAlways, kTspOpaque | (1u << 20));  // use alpha
        s.col(0, 0, 1, 0x80FF0000);
        s.col(64, 0, 1, 0x80FF0000);
        s.col(0, 64, 1, 0x80FF0000, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(2, 2) == ((thr == 0x80 ? 0x8000 : 0) | 0x7C00));
    }
}

// --- cores e sombreamento ---

// Gouraud: centro do pixel (9,9) em (0,0)-(30,0)-(0,30) tem pesos 0.3667/0.3167/0.3167.
// Plano (PCW bit 1 = 0): cor do ultimo vertice.
BS_TEST(render_gouraud_and_flat_shading) {
    {
        Scene s(0);
        s.global(0x80000002u, kIspAlways, kTspOpaque);
        s.col(0, 0, 1, 0xFFFF0000);
        s.col(30, 0, 1, 0xFF00FF00);
        s.col(0, 30, 1, 0xFF0000FF, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(9, 9) == 0x5A8A);   // r 94>>3 = 11, g 81>>2 = 20, b 81>>3 = 10
        BS_CHECK(s.px16(0, 0) == 0xF800 || s.px16(0, 0) == 0xF020);  // junto de A: quase so vermelho
    }
    {
        Scene s(0);
        s.global(0x80000000u, kIspAlways, kTspOpaque);
        s.col(0, 0, 1, 0xFFFF0000);
        s.col(30, 0, 1, 0xFF00FF00);
        s.col(0, 30, 1, 0xFF0000FF, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(9, 9) == 0x001F && s.px16(1, 1) == 0x001F);
    }
}

// --- orientacao e culling (ISP 28-27: 2 descarta area < 0, 3 descarta area > 0) ---

BS_TEST(render_culling_by_orientation) {
    for (uint32_t cull : {0u, 2u, 3u}) {
        Scene s(0);
        s.global(0x80000002u, kIspAlways | (cull << 27), kTspOpaque);
        s.col(0, 0, 1, 0xFF00FF00);  // area > 0 nesta ordem
        s.col(32, 0, 1, 0xFF00FF00);
        s.col(0, 32, 1, 0xFF00FF00, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(3, 3) == (cull == 3 ? 0x0000 : 0x07E0));
    }
}

// Strip de 4 vertices: o 2o triangulo inverte a ordem e cobre o resto do quadrado.
BS_TEST(render_strip_covers_quad) {
    Scene s(0x00FF0000);
    s.global(0x80000002u, kIspAlways | (3u << 27), kTspOpaque);  // descarta area > 0
    s.col(0, 0, 1, 0xFF00FF00);
    s.col(32, 0, 1, 0xFF00FF00);
    s.col(0, 32, 1, 0xFF00FF00);
    s.col(32, 32, 1, 0xFF00FF00, true);
    s.endList();
    s.render();
    // os dois triangulos tem a mesma orientacao efetiva: ambos descartados
    BS_CHECK(s.px16(3, 3) == 0xF800 && s.px16(28, 28) == 0xF800);
}

// --- profundidade ---

// Z-write desligado (ISP bit 26): o 1o triangulo nao grava profundidade, entao o 2o
// (mais distante, compare >=) ainda passa contra o fundo.
BS_TEST(render_depth_write_disable) {
    Scene s(0);
    s.global(0x80000002u, kIspAlways | (1u << 26), kTspOpaque);
    s.col(0, 0, 2, 0xFF0000FF);
    s.col(32, 0, 2, 0xFF0000FF);
    s.col(0, 32, 2, 0xFF0000FF, true);
    s.global(0x80000002u, 6u << 29, kTspOpaque);
    s.col(0, 0, 1, 0xFF00FF00);
    s.col(32, 0, 1, 0xFF00FF00);
    s.col(0, 32, 1, 0xFF00FF00, true);
    s.endList();
    s.render();
    BS_CHECK(s.px16(3, 3) == 0x07E0);
}

// --- blending (lista translucida: SRC_ALPHA, INV_SRC_ALPHA) ---

BS_TEST(render_translucent_alpha_blend) {
    Scene s(0x000000FF);
    s.global(0x82000002u, kIspAlways, (4u << 29) | (5u << 26) | (1u << 20) | 0x00800000u);
    s.col(0, 0, 1, 0x80FFFFFF);
    s.col(64, 0, 1, 0x80FFFFFF);
    s.col(0, 64, 1, 0x80FFFFFF, true);
    s.endList();
    s.render();
    // r = g = 1 * 0.502 = 128; b = 0.502 + 1 * 0.498 = 255
    BS_CHECK(s.px16(3, 3) == ((16u << 11) | (32u << 5) | 31u));
}

// --- texturas e UV ---

void texturedQuad(Scene& s, bool uv16) {
    loadTexture();
    const uint32_t tcw = (1u << 27) | (kTex >> 3);  // 565, twiddled
    s.global(0x8000000Au | (uv16 ? 1u : 0u), kIspAlways | (1u << 25), kTspOpaque, tcw);  // decal, 8x8, ponto
    auto v = [&](float x, float y, float u, float vv, bool end) {
        if (uv16) s.vtx(x, y, 1, (fb(u) & 0xFFFF0000u) | (fb(vv) >> 16), 0, 0xFFFFFFFF, 0, end);
        else s.vtx(x, y, 1, fb(u), fb(vv), 0xFFFFFFFF, 0, end);
    };
    v(0, 0, 0, 0, false);
    v(32, 0, 1, 0, false);
    v(0, 32, 0, 1, false);
    v(32, 32, 1, 1, true);
    s.endList();
    s.render();
}

BS_TEST(render_texture_point_sampling_and_uv16) {
    for (bool uv16 : {false, true}) {
        Scene s(0);
        texturedQuad(s, uv16);
        BS_CHECK(s.px16(2, 10) == 0xF800);   // u = 2.5/32 -> texel 0
        BS_CHECK(s.px16(15, 30) == 0xF800);  // texel 3
        BS_CHECK(s.px16(16, 1) == 0x07E0);   // texel 4
        BS_CHECK(s.px16(30, 20) == 0x07E0);  // texel 7
    }
}

// --- escritas na VRAM: FB_X_CLIP e stride ---

BS_TEST(render_vram_write_respects_clip_and_stride) {
    Scene s(0x00FF0000, 1, 2, 128);  // linha de 128 bytes (maior que a largura)
    s.reg(0x068, 15u << 16);         // FB_X_CLIP 0..15
    for (uint32_t i = 0; i < 32 * 128; i += 2) std::memcpy(vram_data() + kFb + i, "\xAA\xAA", 2);
    s.render();
    BS_CHECK(s.px16(0, 0) == 0xF800 && s.px16(15, 1) == 0xF800);
    BS_CHECK(s.px16(16, 0) == 0xAAAA && s.px16(31, 1) == 0xAAAA);  // fora do clip
    uint16_t pad;
    std::memcpy(&pad, vram_data() + kFb + 1 * 128 + 40 * 2, 2);   // alem da largura na linha
    BS_CHECK(pad == 0xAAAA);
}

// --- linha de base de regressao: cena composta, hash do framebuffer ---
// Gerado pela implementacao atual (2026-10-14). Mudanca intencional no rasterizador:
// conferir a imagem (BS_RENDER_DUMP) e atualizar o hash no STATUS.md e aqui.
BS_TEST(render_composite_scene_hash) {
    Scene s(0x00203040);
    s.global(0x80000002u, kIspAlways, kTspOpaque);
    s.col(2, 3, 1, 0xFFFF0000);
    s.col(29, 6, 1, 0xFF00FF00);
    s.col(8, 27, 1, 0xFF0000FF, true);
    s.endList();  // o tipo de lista so muda depois do fim de lista (TA)
    loadTexture();
    s.global(0x8200000Au, kIspAlways | (1u << 25), (4u << 29) | (5u << 26) | (1u << 20) | 0x00800000u | (3u << 6),
             (1u << 27) | (kTex >> 3));  // translucida, textura modulada por alfa
    s.vtx(10, 10, 2, fb(0), fb(0), 0x80FFFFFF, 0);
    s.vtx(30, 12, 2, fb(1), fb(0), 0x80FFFFFF, 0);
    s.vtx(12, 30, 2, fb(0), fb(1), 0x80FFFFFF, 0, true);
    s.endList();
    s.render();
    const uint64_t h = fnv1a(vram_data() + kFb, 32 * 64);
    if (const char* dump = std::getenv("BS_RENDER_DUMP")) {  // RGB565 32x32 cru, para inspecao
        if (FILE* f = std::fopen(dump, "wb")) {
            std::fwrite(vram_data() + kFb, 1, 32 * 64, f);
            std::fclose(f);
        }
    }
    if (h != 0x1797BA480B2C33A5ull) std::printf("  hash da cena composta: 0x%016llXull\n", (unsigned long long)h);
    BS_CHECK(h == 0x1797BA480B2C33A5ull);
}

// ======== modos (revisao Codex 2026-10-14: cobertura por campo) ========

namespace {

// Textura 8x8 twiddled em kTex com o mesmo texel de 16 bits.
void fillTexture16(uint16_t v) {
    for (uint32_t i = 0; i < 64; ++i) std::memcpy(vram_data() + vram_offset64(kTex + 2 * i), &v, 2);
}

// Quadrado de tela inteira texturizado (strip), cor base/offset empacotadas, U de u0..u1.
void fullQuad(Scene& s, uint32_t pcw, uint32_t isp, uint32_t tsp, uint32_t tcw, uint32_t base, uint32_t off,
              float u0 = 0, float u1 = 1) {
    s.global(pcw, isp, tsp, tcw);
    s.vtx(0, 0, 1, fb(u0), fb(0), base, off);
    s.vtx(32, 0, 1, fb(u1), fb(0), base, off);
    s.vtx(0, 32, 1, fb(u0), fb(1), base, off);
    s.vtx(32, 32, 1, fb(u1), fb(1), base, off, true);
    s.endList();
}

constexpr uint32_t kTcw565  = (1u << 27) | (kTex >> 3);
constexpr uint32_t kTcw4444 = (2u << 27) | (kTex >> 3);

} // namespace

// Instrucoes de sombreamento (TSP 7-6) com texel ARGB4444 0x8F00 (a = 0x88, vermelho) e
// cor base (r, g, b) = 0x80, 0x80, 0xFF. Offset (PCW bit 2) soma a cor de offset.
BS_TEST(render_texture_shading_instructions_and_offset) {
    struct Case {
        uint32_t shading, extraTsp, pcwOffset, expect;
    } cases[] = {
        {0, 0, 0, 0xF800},         // decal: texel
        {1, 0, 0, 0x8000},         // modulate: 1 * 0.502 = 128 -> r 16
        {2, 0, 0, 0xC1EE},         // decal alpha: r 196, g 60, b 119
        {3, 0, 0, 0x8000},         // modulate alpha (rgb igual ao modulate)
        {1, 0, 4, 0xA188},         // modulate + offset 0x203040: r 160, g 48, b 64
        {2, 1u << 19, 0, 0xF800},  // decal alpha com ignore texture alpha: texel
    };
    for (const Case& k : cases) {
        Scene s(0);
        fillTexture16(0x8F00);
        fullQuad(s, 0x8000000Au | k.pcwOffset, kIspAlways, kTspOpaque | (k.shading << 6) | k.extraTsp, kTcw4444,
                 0xFF8080FF, 0x00203040);
        s.render();
        BS_CHECK(s.px16(10, 10) == k.expect);
    }
}

// Filtro bilinear (TSP 14-13 = 1) com meio texel: no pixel 15, u * 8 = 3.75, entao a
// amostra fica a 1/4 entre o texel 3 (preto) e o 4 (branco): 63.75 -> 64 (0x4208).
// Ponto: o mesmo pixel le o texel 3 (preto).
BS_TEST(render_texture_bilinear_and_point) {
    for (uint32_t filter : {0u, 1u}) {
        Scene s(0);
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 8; ++x) {
                const uint16_t v = x < 4 ? 0x0000 : 0xFFFF;
                std::memcpy(vram_data() + vram_offset64(kTex + 2 * pvr_twiddle(x, y, 8, 8)), &v, 2);
            }
        fullQuad(s, 0x8000000Au, kIspAlways, kTspOpaque | (filter << 13), kTcw565, 0xFFFFFFFF, 0, -0.015625f,
                 0.984375f);
        s.render();
        BS_CHECK(s.px16(15, 10) == (filter ? 0x4208 : 0x0000));
    }
}

// U fora de [0, 1): repetir, clamp (TSP 16) e espelhar (TSP 18). Textura: x < 4 vermelho.
// Quadrado com u de 0 a 2: pixel 20 -> texel 10, pixel 28 -> texel 14.
BS_TEST(render_texture_wrap_clamp_flip) {
    struct Case {
        uint32_t tsp;
        uint16_t at20, at28;
    } cases[] = {{0, 0xF800, 0x07E0}, {1u << 16, 0x07E0, 0x07E0}, {1u << 18, 0x07E0, 0xF800}};
    for (const Case& k : cases) {
        Scene s(0);
        loadTexture();
        fullQuad(s, 0x8000000Au, kIspAlways, kTspOpaque | k.tsp, kTcw565, 0xFFFFFFFF, 0, 0, 2);
        s.render();
        BS_CHECK(s.px16(20, 5) == k.at20 && s.px16(28, 5) == k.at28);
    }
}

// Paleta de 8 bpp: seletor 0x10 -> bloco (0x10 >> 4) * 256; indice 5 -> entrada 261.
BS_TEST(render_texture_palette_8bpp) {
    Scene s(0);
    s.reg(0x108, 1);                // PAL_RAM_CTRL: 565
    s.reg(0x1000 + 4 * 261, 0x001F);
    s.reg(0x1000 + 4 * 5, 0xF800);  // bloco 0 (seletor ignorado daria vermelho)
    for (uint32_t i = 0; i < 64; ++i) vram_data()[vram_offset64(kTex + i)] = 5;
    fullQuad(s, 0x8000000Au, kIspAlways, kTspOpaque, (6u << 27) | (0x10u << 21) | (kTex >> 3), 0xFFFFFFFF, 0);
    s.render();
    BS_CHECK(s.px16(10, 10) == 0x001F);
}

// Textura linear (TCW 26) com stride (TCW 25, TEXT_CONTROL = 1: 32 texels por linha).
// A linha 3 da textura comeca no texel 96; sem stride seria o 24 (azul).
BS_TEST(render_texture_linear_stride) {
    Scene s(0);
    s.reg(0x0E4, 1);
    for (uint32_t i = 0; i < 32 * 8; ++i) {
        const uint16_t v = 0x001F;
        std::memcpy(vram_data() + vram_offset64(kTex + 2 * i), &v, 2);
    }
    for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t x = 0; x < 8; ++x) {
            const uint16_t v = x < 4 ? 0xF800 : 0x07E0;
            std::memcpy(vram_data() + vram_offset64(kTex + 2 * (y * 32 + x)), &v, 2);
        }
    fullQuad(s, 0x8000000Au, kIspAlways, kTspOpaque, (1u << 27) | (1u << 26) | (1u << 25) | (kTex >> 3), 0xFFFFFFFF,
             0);
    s.render();
    BS_CHECK(s.px16(2, 14) == 0xF800 && s.px16(30, 14) == 0x07E0);
}

// Fatores de blend (TSP 31-29 origem, 28-26 destino). Origem: (a, r) = (0.502, 1);
// destino (fundo): (a, r) = (1, 0.251). Campo R do 565 = r >> 3.
BS_TEST(render_blend_factors) {
    const uint32_t srcR[8] = {0, 31, 8, 23, 16, 15, 31, 0};  // destino ZERO
    const uint32_t dstR[8] = {0, 8, 8, 0, 4, 4, 8, 0};       // origem ZERO
    for (uint32_t f = 0; f < 8; ++f)
        for (int side = 0; side < 2; ++side) {
            Scene          s(0x004080FF);
            const uint32_t tsp = (side == 0 ? (f << 29) : (f << 26)) | (1u << 20) | 0x00800000u;
            s.global(0x82000002u, kIspAlways, tsp);
            s.col(0, 0, 1, 0x80FF0000);
            s.col(64, 0, 1, 0x80FF0000);
            s.col(0, 64, 1, 0x80FF0000, true);
            s.endList();
            s.render();
            BS_CHECK(uint32_t(s.px16(3, 3) >> 11) == (side == 0 ? srcR[f] : dstR[f]));
        }
}

// Punch-through: passa com alfa >= PT_ALPHA_REF.
BS_TEST(render_punch_through_alpha_ref) {
    for (uint32_t alpha : {0x80u, 0x7Fu}) {
        Scene s(0x000000FF);
        s.reg(0x11C, 0x80);
        s.global(0x84000002u, kIspAlways, kTspOpaque | (1u << 20));
        s.col(0, 0, 1, (alpha << 24) | 0xFF0000);
        s.col(64, 0, 1, (alpha << 24) | 0xFF0000);
        s.col(0, 64, 1, (alpha << 24) | 0xFF0000, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(3, 3) == (alpha >= 0x80 ? 0xF800 : 0x001F));
    }
}

// Os 8 compares de profundidade (ISP 31-29), novo z contra 0.5 ja gravado:
// 0 nunca, 1 <, 2 ==, 3 <=, 4 >, 5 !=, 6 >=, 7 sempre.
BS_TEST(render_depth_compare_modes) {
    const bool  expect[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
    const float z2[3]        = {0.25f, 0.5f, 0.75f};
    for (uint32_t cmp = 0; cmp < 8; ++cmp)
        for (int k = 0; k < 3; ++k) {
            Scene s(0);
            s.global(0x80000002u, kIspAlways, kTspOpaque);
            s.col(0, 0, 0.5f, 0xFFFF0000);
            s.col(64, 0, 0.5f, 0xFFFF0000);
            s.col(0, 64, 0.5f, 0xFFFF0000, true);
            s.global(0x80000002u, cmp << 29, kTspOpaque);
            s.col(0, 0, z2[k], 0xFF00FF00);
            s.col(64, 0, z2[k], 0xFF00FF00);
            s.col(0, 64, z2[k], 0xFF00FF00, true);
            s.endList();
            s.render();
            BS_CHECK(s.px16(3, 3) == (expect[cmp][k] ? 0x07E0 : 0xF800));
        }
}

// Cor float (tipo 1), intensidade 1 (cor de face no global de 32 bytes) e intensidade 2
// (mantem a cor de face do ultimo global de intensidade 1).
BS_TEST(render_float_and_intensity_colors) {
    {
        Scene s(0);
        s.global(0x80000012u, kIspAlways, kTspOpaque);
        s.vtx(0, 0, 1, fb(1), fb(0), fb(1), fb(0));
        s.vtx(64, 0, 1, fb(1), fb(0), fb(1), fb(0));
        s.vtx(0, 64, 1, fb(1), fb(0), fb(1), fb(0), true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(3, 3) == 0x07E0);
    }
    {
        Scene s(0);
        s.ta({0x80000022u, kIspAlways, kTspOpaque, 0, fb(1), fb(1), fb(0.5f), fb(0)});  // face (1, 1, 0.5, 0)
        s.vtx(0, 0, 1, 0, 0, fb(0.5f), 0);
        s.vtx(64, 0, 1, 0, 0, fb(0.5f), 0);
        s.vtx(0, 64, 1, 0, 0, fb(0.5f), 0, true);
        s.ta({0x80000032u, kIspAlways, kTspOpaque, 0, fb(1), fb(0), fb(0), fb(1)});  // modo 2: w4..w7 ignorados
        s.vtx(0, 0, 0.5f, 0, 0, fb(1), 0);
        s.vtx(64, 0, 0.5f, 0, 0, fb(1), 0);
        s.vtx(0, 64, 0.5f, 0, 0, fb(1), 0, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(3, 3) == 0xFC00);  // 2o triangulo (o mais recente): (1, 0.5, 0)
    }
}

namespace {
// Sprite: global tipo 5 (cor base em w4) e vertice de 64 bytes (A, B, C, D xy).
void sprite(Scene& s, uint32_t isp, uint32_t base, float ax, float ay, float az, float bx, float by, float bz,
            float cx, float cy, float cz, float dx, float dy) {
    s.ta({0xA0000000u, isp, kTspOpaque, 0, base, 0});
    s.ta({0xE0000000u, fb(ax), fb(ay), fb(az), fb(bx), fb(by), fb(bz), fb(cx)});
    s.ta({fb(cy), fb(cz), fb(dx), fb(dy), 0, 0, 0, 0});
}
} // namespace

// Sprite cobre o quadrilatero A, B, C, D.
BS_TEST(render_sprite_quad_coverage) {
    Scene s(0x000000FF);
    sprite(s, kIspAlways, 0xFF00FF00, 0, 0, 1, 32, 0, 1, 32, 32, 1, 0, 32);
    s.endList();
    s.render();
    BS_CHECK(s.px16(1, 30) == 0x07E0 && s.px16(30, 1) == 0x07E0);
}

// z de D pelo plano de A, B, C (nao A + C - B). A(0,0,1) B(32,0,1) C(32,32,3) D(0,16):
// plano z = 1 + y/16, z(D) = 2 (A + C - B daria 3). Contra um triangulo em z = 2.2 com
// compare ">": o pixel (1,14) fica com z = 1.906 (nao passa; com z(D) = 3 seria 2.72 e
// passaria), e (30,29) com z = 2.84 passa.
BS_TEST(render_sprite_fourth_vertex_from_plane) {
    Scene s(0);
    s.global(0x80000002u, kIspAlways, kTspOpaque);
    s.col(0, 0, 2.2f, 0xFFFF0000);
    s.col(64, 0, 2.2f, 0xFFFF0000);
    s.col(0, 64, 2.2f, 0xFFFF0000, true);
    sprite(s, 4u << 29, 0xFF00FF00, 0, 0, 1, 32, 0, 1, 32, 32, 3, 0, 16);
    s.endList();
    s.render();
    BS_CHECK(s.px16(1, 14) == 0xF800);
    BS_CHECK(s.px16(30, 29) == 0x07E0);
}

// HALF_OFFSET bit 0: amostra de cobertura no canto (0) ou no centro (1) do pixel.
// Triangulo (0,0) (32,0) (0,32): os pixels (31,0) e (15,16) tem canto em x + y = 31
// (dentro) e centro em x + y = 32 (aresta direita, fora).
BS_TEST(render_half_offset_sample_position) {
    for (uint32_t half : {0u, 7u}) {
        Scene s(0x000000FF);
        s.reg(0x080, half);
        s.global(0x80000002u, kIspAlways, kTspOpaque);
        s.col(0, 0, 1, 0xFF00FF00);
        s.col(32, 0, 1, 0xFF00FF00);
        s.col(0, 32, 1, 0xFF00FF00, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(31, 0) == (half ? 0x001F : 0x07E0));
        BS_CHECK(s.px16(15, 16) == (half ? 0x001F : 0x07E0));
    }
}

// Color clamp do TSP (bit 21): a cor final fica entre FPU_CLAMP_MIN e FPU_CLAMP_MAX.
BS_TEST(render_tsp_color_clamp) {
    for (uint32_t on : {0u, 1u}) {
        Scene s(0);
        s.reg(0x0BC, 0xFF80FFFF);  // FPU_CLAMP_MAX: r <= 0x80
        s.reg(0x0C0, 0x00000020);  // FPU_CLAMP_MIN: b >= 0x20
        s.global(0x80000002u, kIspAlways, kTspOpaque | (on << 21));
        s.col(0, 0, 1, 0xFFFF0000);
        s.col(64, 0, 1, 0xFFFF0000);
        s.col(0, 64, 1, 0xFFFF0000, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(3, 3) == (on ? ((16u << 11) | 4u) : 0xF800));
    }
}

// Autosort (ISP_FEED_CFG bit 0 = 0): translucidos misturados do mais distante para o
// mais proximo em cada pixel; com bit 0 = 1 (pre-sort), na ordem de submissao.
// Fundo azul; submetidos: vermelho perto (z 2), depois verde longe (z 1), alfa 0x80.
// Autosort: verde, depois vermelho -> r 128, g 64, b 63 = 0x8207.
// Pre-sort: vermelho, depois verde -> r 64, g 128, b 63 = 0x4407.
// (Encontrado na comparacao com o Flycast: o painel da janela e o texto sao
// translucidos e o texto vem antes; sem autosort, as celulas do texto escondiam o painel.)
BS_TEST(render_translucent_autosort_vs_presort) {
    for (uint32_t presort : {0u, 1u}) {
        Scene s(0x000000FF);
        s.reg(0x098, presort);
        const uint32_t tsp = (4u << 29) | (5u << 26) | (1u << 20) | 0x00800000u;
        s.global(0x82000002u, 7u << 29, tsp);  // compare sempre: isola a ordem de mistura
        s.col(0, 0, 2, 0x80FF0000);
        s.col(64, 0, 2, 0x80FF0000);
        s.col(0, 64, 2, 0x80FF0000, true);
        s.global(0x82000002u, 7u << 29, tsp);
        s.col(0, 0, 1, 0x8000FF00);
        s.col(64, 0, 1, 0x8000FF00);
        s.col(0, 64, 1, 0x8000FF00, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(3, 3) == (presort ? 0x4407 : 0x8207));
    }
}

// Cor de offset em float negativa (cenas 3D do jogo depois de START): o TA guarda as cores
// em 8 bits por canal, entao o offset satura em 0 e nao subtrai. Intensidade 1 com
// textura e offset (global de 64 bytes): face (1,1,1,1), offset de face (1,-1,-1,-1),
// textura vermelha, modulate -> vermelho (sem a saturacao, daria preto).
BS_TEST(render_negative_float_offset_saturates_to_zero) {
    Scene s(0);
    loadTexture();
    const uint32_t pcw = 0x8000002Eu;  // textura + offset + intensidade 1 + gouraud
    s.ta({pcw, kIspAlways, kTspOpaque | (1u << 6), (1u << 27) | (kTex >> 3), 0, 0, 0, 0});
    s.ta({fb(1), fb(1), fb(1), fb(1), fb(1), fb(-1), fb(-1), fb(-1)});
    s.vtx(0, 0, 1, fb(0), fb(0), fb(1), fb(1));
    s.vtx(64, 0, 1, fb(0.25f), fb(0), fb(1), fb(1));
    s.vtx(0, 64, 1, fb(0), fb(0.25f), fb(1), fb(1), true);
    s.endList();
    s.render();
    BS_CHECK(s.px16(3, 3) == 0xF800);
}

// ======== revisao Codex 2026-10-14 10:56 (manual do Dreamcast 3.4.2 e 3.4.3) ========

// Autosort: o compare do poligono e ignorado e vale >=. Opaco perto (z 2, vermelho);
// translucido atras (z 1, verde, ISP ALWAYS): rejeitado em autosort; em pre-sort, o
// ALWAYS vale e o verde e misturado (0.5 verde + 0.5 vermelho).
BS_TEST(render_autosort_forces_gequal) {
    for (uint32_t presort : {0u, 1u}) {
        Scene s(0);
        s.reg(0x098, presort);
        s.global(0x80000002u, kIspAlways, kTspOpaque);
        s.col(0, 0, 2, 0xFFFF0000);
        s.col(64, 0, 2, 0xFFFF0000);
        s.col(0, 64, 2, 0xFFFF0000, true);
        s.endList();
        s.global(0x82000002u, kIspAlways, (4u << 29) | (5u << 26) | (1u << 20) | 0x00800000u);
        s.col(0, 0, 1, 0x8000FF00);
        s.col(64, 0, 1, 0x8000FF00);
        s.col(0, 64, 1, 0x8000FF00, true);
        s.endList();
        s.render();
        // pre-sort: r = 1 * 0.498 = 127 -> 15; g = 0.502 -> 128 -> 32
        BS_CHECK(s.px16(3, 3) == (presort ? ((15u << 11) | (32u << 5)) : 0xF800));
    }
}

// Punch-through ordenado da frente para tras com >= (compare do poligono ignorado):
// submetidos perto (z 2, vermelho) e depois longe (z 1, verde), ambos ISP ALWAYS ->
// vence o vermelho (na ordem de submissao com ALWAYS, venceria o verde). Com o perto
// abaixo de PT_ALPHA_REF, ele e descartado e revela o verde.
BS_TEST(render_punch_through_front_to_back) {
    for (uint32_t nearAlpha : {0xFFu, 0x40u}) {
        Scene s(0x000000FF);
        s.reg(0x11C, 0x80);
        s.global(0x84000002u, kIspAlways, kTspOpaque | (1u << 20));
        s.col(0, 0, 2, (nearAlpha << 24) | 0xFF0000);
        s.col(64, 0, 2, (nearAlpha << 24) | 0xFF0000);
        s.col(0, 64, 2, (nearAlpha << 24) | 0xFF0000, true);
        s.global(0x84000002u, kIspAlways, kTspOpaque | (1u << 20));
        s.col(0, 0, 1, 0xFF00FF00);
        s.col(64, 0, 1, 0xFF00FF00);
        s.col(0, 64, 1, 0xFF00FF00, true);
        s.endList();
        s.render();
        BS_CHECK(s.px16(3, 3) == (nearAlpha >= 0x80 ? 0xF800 : 0x07E0));
    }
}

// Sprite texturizado com z nao uniforme: A(0,0,z 1,u 0) B(32,0,z 1,u 1) C(32,32,z 4,u 1)
// D(0,32). Pelos planos de z e u*z: z(D) = 4, u(D) = 3/4. A + C - B daria u(D) = 0.
// Textura: x < 4 vermelho. Perto de D, u ~ 0.7 (verde); com u(D) = 0, ~0 (vermelho).
BS_TEST(render_sprite_fourth_vertex_uv_perspective) {
    Scene s(0);
    loadTexture();
    auto uv = [](float u, float v) { return (fb(u) & 0xFFFF0000u) | (fb(v) >> 16); };
    s.ta({0xA0000008u, kIspAlways, kTspOpaque, kTcw565, 0xFFFFFFFF, 0});
    s.ta({0xE0000000u, fb(0), fb(0), fb(1), fb(32), fb(0), fb(1), fb(32)});
    s.ta({fb(32), fb(4), fb(0), fb(32), 0, uv(0, 0), uv(1, 0), uv(1, 1)});
    s.endList();
    s.render();
    BS_CHECK(s.px16(1, 30) == 0x07E0);
    BS_CHECK(s.px16(5, 1) == 0xF800);  // perto de A: u ~ 0.27
}

// HALF_OFFSET bit 1 (amostra dos atributos): gradiente de cor de 0 (x = 0) a 1 (x = 32)
// num framebuffer 8888; no pixel 10, canto -> 10/32 * 255 = 79.7 -> 0x50, centro
// -> 10.5/32 * 255 = 83.7 -> 0x54. Cobertura sempre no centro (bit 0).
BS_TEST(render_half_offset_attribute_sample) {
    for (uint32_t half : {1u, 3u}) {
        Scene s(0, 6, 4);
        s.reg(0x080, half);
        s.global(0x80000002u, kIspAlways, kTspOpaque);
        s.col(0, -32, 1, 0xFF000000);
        s.col(32, -32, 1, 0xFFFF0000);
        s.col(0, 96, 1, 0xFF000000);
        s.col(32, 96, 1, 0xFFFF0000, true);
        s.endList();
        s.render();
        const uint32_t r = (s.px32(10, 5) >> 16) & 0xFF;
        BS_CHECK(r == (half == 1 ? 0x50u : 0x54u));
    }
}

// HALF_OFFSET bit 2 (meio texel) com amostragem por ponto: no pixel 14, u * 8 = 3.625.
// Centro do texel (bit 2 = 1): texel 3 (vermelho). Canto (bit 2 = 0): mais proximo de
// 3.625 + 0.5 -> texel 4 (verde).
BS_TEST(render_half_offset_texel_center) {
    for (uint32_t half : {3u, 7u}) {
        Scene s(0);
        s.reg(0x080, half);
        loadTexture();
        fullQuad(s, 0x8000000Au, kIspAlways, kTspOpaque, kTcw565, 0xFFFFFFFF, 0);
        s.render();
        BS_CHECK(s.px16(14, 10) == (half == 7 ? 0xF800 : 0x07E0));
    }
}

// Punch-through grava profundidade mesmo com z-write disable (ISP bit 26): o translucido
// atras dele (z 1.5 < 2, compare forcado >= em autosort) e rejeitado; o pixel fica
// vermelho. Se o z-write disable valesse, o verde seria misturado por cima.
BS_TEST(render_punch_through_ignores_zwrite_disable) {
    Scene s(0x000000FF);
    s.reg(0x11C, 0x80);
    s.global(0x84000002u, kIspAlways | (1u << 26), kTspOpaque | (1u << 20));
    s.col(0, 0, 2, 0xFFFF0000);
    s.col(64, 0, 2, 0xFFFF0000);
    s.col(0, 64, 2, 0xFFFF0000, true);
    s.endList();
    s.global(0x82000002u, kIspAlways, (4u << 29) | (5u << 26) | (1u << 20) | 0x00800000u);
    s.col(0, 0, 1.5f, 0x8000FF00);
    s.col(64, 0, 1.5f, 0x8000FF00);
    s.col(0, 64, 1.5f, 0x8000FF00, true);
    s.endList();
    s.render();
    BS_CHECK(s.px16(3, 3) == 0xF800);
}
