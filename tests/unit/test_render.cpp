// Texturas (runtime/pvr/pvr_texture.cpp) e rasterizador de referencia
// (runtime/pvr/pvr_render.cpp), alimentados pelo caminho do jogo: registradores do PVR
// pela memoria do guest, parametros pela FIFO do TA e escrita em STARTRENDER.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "cpu/time_hooks.h"
#include "pvr/holly_intc.h"
#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "pvr/vram.h"
#include "render_capture.h"

#include <cstring>
#include <vector>

using namespace bs;

namespace {

uint32_t fbits(float f) {
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}

struct RenderFixture {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};

    // Tela de 32x32 (1 tile), framebuffer RGB565 em 0x100000 (VRAM de 32 bits),
    // fundo pelo plano em PARAM_BASE (0x000000) com tag 0, skip 1 e cor `bg`.
    explicit RenderFixture(uint32_t bg) {
        c.ram = ram.data();
        vram_reset();
        pvr_regs_reset();
        ta_reset();
        reg(0x13C, 0);                       // TA_GLOB_TILE_CLIP: 1x1 tile
        reg(0x060, 0x100000);                // FB_W_SOF1
        reg(0x04C, 32 * 2 / 8);              // FB_W_LINESTRIDE (unidades de 8 bytes)
        reg(0x048, 1);                       // FB_W_CTRL: 565
        reg(0x068, 31u << 16);               // FB_X_CLIP 0..31
        reg(0x06C, 31u << 16);               // FB_Y_CLIP 0..31
        reg(0x020, 0);                       // PARAM_BASE
        reg(0x08C, 1u << 24);                // ISP_BACKGND_T: skip 1, tag 0
        reg(0x088, fbits(0.0001f));          // ISP_BACKGND_D
        reg(0x080, 7);                       // HALF_OFFSET: amostras no centro (valor do jogo)
        const uint32_t plane[3 + 3 * 4] = {0, 0, 0, 0, 0, 1, bg, 32, 0, 1, bg, 0, 32, 1, bg};
        std::memcpy(vram_data(), plane, sizeof plane);
        reg(0x144, 0x80000000);              // TA_LIST_INIT
#if !BS_VALIDACAO
        // N8.7: no produto, o render vai pela IR (o renderizador em software sobre a captura); na validacao, o
        // rasterizador do caminho do TA. As mesmas provas valem para os dois
        transitional::render_capture::install(transitional::render_capture::Mode::Software);
#endif
    }
    ~RenderFixture() { transitional::render_capture::install(transitional::render_capture::Mode::Off); }

    void reg(uint32_t off, uint32_t v) { sh4_write32(&c, 0xA05F8000 + off, v); }

    void ta(std::initializer_list<uint32_t> words) {
        uint32_t d[8] = {};
        std::copy(words.begin(), words.end(), d);
        BS_CHECK(ta_fifo_write(&c, 0x10000000, d));
    }

    // Poligono opaco sem textura, cor empacotada, gouraud
    void opaqueGlobal(uint32_t isp) { ta({0x80000002u, isp, 0x20800000u, 0}); }
    void vertex(float x, float y, float z, uint32_t argb, bool end = false) {
        ta({0xE0000000u | (end ? 1u << 28 : 0), fbits(x), fbits(y), fbits(z), 0, 0, argb});
    }
    void endList() { ta({0}); }
    void render() { reg(0x014, 0xFFFFFFFF); }

    uint16_t px(int x, int y) {
        uint16_t v;
        std::memcpy(&v, vram_data() + 0x100000 + y * 64 + x * 2, 2);
        return v;
    }
};

} // namespace

#if BS_VALIDACAO  // N8.7: o decodificador de texels da VRAM (pvr_texel.cpp) so existe na validacao
BS_TEST(pvr_twiddle_interleaves_y_in_even_bits) {
    BS_CHECK(pvr_twiddle(0, 1, 8, 8) == 1);
    BS_CHECK(pvr_twiddle(1, 0, 8, 8) == 2);
    BS_CHECK(pvr_twiddle(1, 1, 8, 8) == 3);
    BS_CHECK(pvr_twiddle(0, 2, 8, 8) == 4);
    BS_CHECK(pvr_twiddle(7, 7, 8, 8) == 63);
    // retangular: quadrados de min(w, h) lado a lado
    BS_CHECK(pvr_twiddle(8, 0, 16, 8) == 64);
    BS_CHECK(pvr_twiddle(0, 8, 8, 16) == 64);
}

BS_TEST(pvr_texel16_formats_expand_to_argb8888) {
    BS_CHECK(pvr_texel16_to_argb(0xFC00, 0) == 0xFFFF0000);  // 1555 vermelho opaco
    BS_CHECK(pvr_texel16_to_argb(0x001F, 0) == 0x000000FF);  // 1555 azul transparente
    BS_CHECK(pvr_texel16_to_argb(0x07E0, 1) == 0xFF00FF00);  // 565 verde
    BS_CHECK(pvr_texel16_to_argb(0x8F00, 2) == 0x88FF0000);  // 4444
}

// Paleta de 4 bits twiddled: nibble baixo primeiro; seletor de paleta * 16.
BS_TEST(pvr_texel_pal4_uses_low_nibble_first_and_selector) {
    RenderFixture f(0);
    f.reg(0x108, 1);  // PAL_RAM_CTRL: 565
    f.reg(0x1000 + 4 * (16 + 1), 0xF800);
    f.reg(0x1000 + 4 * (16 + 2), 0x001F);
    const uint32_t texAddr = 0x200000;
    vram_data()[vram_offset64(texAddr)] = 0x21;  // texel 0 = 1, texel 1 = 2
    TexInfo t;
    t.tcw = (5u << 27) | (1u << 21) | (texAddr >> 3);
    BS_CHECK(pvr_texel(t, 0, 0) == 0xFFFF0000);
    BS_CHECK(pvr_texel(t, 0, 1) == 0xFF0000FF);  // indice twiddled 1 = (x 0, y 1)
}

#endif

BS_TEST(render_background_plane_fills_framebuffer) {
    RenderFixture f(0x00FF0000);
    f.render();
    BS_CHECK(f.px(0, 0) == 0xF800 && f.px(31, 31) == 0xF800);
}

// N8.7 3a (revisao Codex 20261028-212527, D-K4): o STARTRENDER pela escrita do jogo agenda o fim de render no instante da
// escrita + min(450000 + bytes do TA * 100, 1500000); um segundo render substitui o prazo; no prazo, ISTNRM 0-2 acendem.
// Vale nos dois builds (produto: FrameState; validacao: o banco do legado).
BS_TEST(render_startrender_schedules_render_done_with_the_ta_bytes) {
    RenderFixture f(0x00FF0000);
    f.opaqueGlobal(7u << 29);
    f.vertex(0, 0, 1, 0xFF00FF00);
    f.vertex(32, 0, 1, 0xFF00FF00);
    f.vertex(0, 32, 1, 0xFF00FF00, true);
    f.endList();
    BS_CHECK(ta_bytes() == 5 * 32);
    const uint64_t t0 = time_now(&f.c);
    f.render();
    BS_CHECK(pvr_render_done_due() == t0 + 450000 + 5 * 32 * 100);
    f.c.cycles += 1000;
    const uint64_t t1 = time_now(&f.c);
    f.render();  // o segundo prazo substitui o primeiro
    BS_CHECK(pvr_render_done_due() == t1 + 450000 + 5 * 32 * 100 && t1 > t0);
    // muitos bytes: o teto de 1 500 000
    for (int i = 0; i < 400; ++i) f.ta({0x80000002u, 7u << 29, 0x20800000u, 0});
    f.render();
    BS_CHECK(pvr_render_done_due() == time_now(&f.c) + 1500000);
    const uint64_t due = pvr_render_done_due();
    f.c.cycles += due - time_now(&f.c);
    holly_intc_sync(time_now(&f.c));
    BS_CHECK((sh4_read32(&f.c, 0xA05F6900) & 7) == 7 && pvr_render_done_due() == 0);
}

// Triangulo (0,0) (32,0) (0,32): amostra no centro do pixel; diagonal x + y + 1 < 32.
BS_TEST(render_opaque_triangle_covers_pixel_centers) {
    RenderFixture f(0x00FF0000);
    f.opaqueGlobal(7u << 29);  // depth compare: sempre
    f.vertex(0, 0, 1, 0xFF00FF00);
    f.vertex(32, 0, 1, 0xFF00FF00);
    f.vertex(0, 32, 1, 0xFF00FF00, true);
    f.endList();
    f.render();
    BS_CHECK(f.px(0, 0) == 0x07E0);
    BS_CHECK(f.px(15, 15) == 0x07E0);  // 15.5 + 15.5 = 31 < 32
    BS_CHECK(f.px(16, 16) == 0xF800);
    BS_CHECK(f.px(31, 0) == 0xF800);   // 31.5 + 0.5 = 32: na hipotenusa (aresta direita): fora
}

// Profundidade 1/w: com "maior ou igual", o triangulo mais proximo (z maior) vence.
BS_TEST(render_depth_compare_keeps_nearest) {
    RenderFixture f(0);
    f.opaqueGlobal(6u << 29);  // >=
    f.vertex(0, 0, 2, 0xFF0000FF);
    f.vertex(32, 0, 2, 0xFF0000FF);
    f.vertex(0, 32, 2, 0xFF0000FF, true);
    f.vertex(0, 0, 1, 0xFF00FF00);
    f.vertex(32, 0, 1, 0xFF00FF00);
    f.vertex(0, 32, 1, 0xFF00FF00, true);
    f.endList();
    f.render();
    BS_CHECK(f.px(2, 2) == 0x001F);
}

// Mesma cobertura com o sentido invertido (sem culling): a regra top-left nao depende
// da ordem dos vertices.
BS_TEST(render_fill_rule_independent_of_winding) {
    RenderFixture f(0x00FF0000);
    f.opaqueGlobal(7u << 29);
    f.vertex(0, 0, 1, 0xFF00FF00);
    f.vertex(0, 32, 1, 0xFF00FF00);
    f.vertex(32, 0, 1, 0xFF00FF00, true);
    f.endList();
    f.render();
    BS_CHECK(f.px(0, 0) == 0x07E0 && f.px(15, 15) == 0x07E0);
    BS_CHECK(f.px(31, 0) == 0xF800 && f.px(0, 31) == 0xF800 && f.px(16, 16) == 0xF800);
}

// FB_W_CTRL: K fica nos bits 15-8 (e o limiar de alfa nos 23-16). No 0555, o bit 15
// gravado e o bit 7 de K (revisao Codex 2026-10-14: os campos estavam trocados).
BS_TEST(render_fb_0555_uses_k_from_bits_15_8) {
    RenderFixture f(0x00FF0000);
    f.reg(0x048, (0x80u << 8) | (0x00u << 16));  // 0555, K = 0x80, limiar = 0
    f.render();
    BS_CHECK(f.px(3, 3) == 0xFC00);
    RenderFixture g(0x00FF0000);
    g.reg(0x048, (0x00u << 8) | (0xFFu << 16));  // K = 0, limiar = 0xFF
    g.render();
    BS_CHECK(g.px(3, 3) == 0x7C00);
}

namespace {
// Tela de 2x1 tiles (64x32) para o recorte do usuario; pixel em 0x100000, linha de 128 bytes.
uint16_t px64(int x, int y) {
    uint16_t v;
    std::memcpy(&v, vram_data() + 0x100000 + y * 128 + x * 2, 2);
    return v;
}
void clipScene(RenderFixture& f, uint32_t mode) {
    f.reg(0x13C, 1);           // TA_GLOB_TILE_CLIP: 2x1 tiles
    f.reg(0x04C, 64 * 2 / 8);  // FB_W_LINESTRIDE
    f.reg(0x068, 63u << 16);   // FB_X_CLIP 0..63
    f.ta({0x20000000u, 0, 0, 0, 1, 0, 1, 0});  // recorte do usuario: so o tile x = 1
    f.ta({0x80000002u | (mode << 16), 7u << 29, 0x20800000u, 0});
    f.vertex(0, 0, 1, 0xFF00FF00);
    f.vertex(200, 0, 1, 0xFF00FF00);
    f.vertex(0, 200, 1, 0xFF00FF00, true);
    f.endList();
    f.render();
}
} // namespace

// PCW bits 17-16: 2 desenha so dentro do recorte, 3 so fora.
BS_TEST(render_user_clip_inside_and_outside) {
    RenderFixture in(0x00FF0000);
    clipScene(in, 2);
    BS_CHECK(px64(10, 5) == 0xF800 && px64(31, 5) == 0xF800);  // tile 0: fora, fundo
    BS_CHECK(px64(32, 5) == 0x07E0 && px64(40, 20) == 0x07E0);  // tile 1: dentro
    RenderFixture out(0x00FF0000);
    clipScene(out, 3);
    BS_CHECK(px64(10, 5) == 0x07E0 && px64(31, 5) == 0x07E0);
    BS_CHECK(px64(32, 5) == 0xF800 && px64(40, 20) == 0xF800);
}
