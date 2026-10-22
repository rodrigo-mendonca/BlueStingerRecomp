// N7a: captura da IR de render (transitional/render_capture) a partir do decodificador do TA, em cenas sinteticas
// pelo caminho do jogo (registradores, FIFO do TA, STARTRENDER). O modo conferir prova em cada render que os
// triangulos da IR sao os desenhados e que a imagem redesenhada a partir dela e identica.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "pvr/pvr_regs.h"
#include "pvr/ta.h"
#include "pvr/vram.h"
#include "render_capture.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace bs;
namespace rc = bs::transitional::render_capture;
namespace nr = bs::native::render;

namespace {

uint32_t fb(float f) {
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}

constexpr uint32_t kIspAlways = 7u << 29;
constexpr uint32_t kTspOpaque = 0x20800000u;

// Tela de 32x32, fundo pelo plano em PARAM_BASE 0, framebuffer 565 em 0x100000; captura no modo conferir.
struct CaptureScene {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    rc::Stats            before;

    CaptureScene() {
        c.ram = ram.data();
        vram_reset();
        pvr_regs_reset();
        ta_reset();
        reg(0x13C, 0);
        reg(0x060, 0x100000);
        reg(0x04C, 32 * 2 / 8);
        reg(0x048, 1);
        reg(0x068, 31u << 16);
        reg(0x06C, 31u << 16);
        reg(0x020, 0);
        reg(0x08C, 1u << 24);
        reg(0x088, fb(0.0001f));
        reg(0x080, 7);
        const uint32_t plane[3 + 3 * 4] = {0, 0, 0, 0, 0, 1, 0xFF102030, 32, 0, 1, 0xFF102030, 0, 32, 1, 0xFF102030};
        std::memcpy(vram_data(), plane, sizeof plane);
        reg(0x144, 0x80000000);
        rc::install(rc::Mode::Check);
        before = rc::stats();
    }
    ~CaptureScene() { rc::install(rc::Mode::Off); }

    void reg(uint32_t off, uint32_t v) { sh4_write32(&c, 0xA05F8000 + off, v); }
    void ta(std::initializer_list<uint32_t> words) {
        uint32_t d[8] = {};
        std::copy(words.begin(), words.end(), d);
        BS_CHECK(ta_fifo_write(&c, 0x10000000, d));
    }
    void vtx(float x, float y, float z, uint32_t w4, uint32_t w5, uint32_t w6, bool end = false) {
        ta({0xE0000000u | (end ? 1u << 28 : 0), fb(x), fb(y), fb(z), w4, w5, w6, 0});
    }
    void endList() { ta({0}); }
    void render() { reg(0x014, 1); }

    // conferido e sem divergencias desde o inicio da cena
    void expectClean() {
        const rc::Stats& s = rc::stats();
        BS_CHECK(s.checked == before.checked + 1);
        BS_CHECK(s.invalid == before.invalid);
        BS_CHECK(s.triangle_mismatch == before.triangle_mismatch);
        BS_CHECK(s.image_mismatch == before.image_mismatch);
        BS_CHECK(s.texture_mismatch == before.texture_mismatch);
    }
};

// longe do framebuffer (0x100000 no caminho de 32 bits = 0x200000.. no de 64): o id e lido depois do render
constexpr uint32_t kTexA = 0x400000, kTexB = 0x410000;
void loadTexture(uint32_t base, uint16_t seed) {  // 8x8 RGB565 twiddled
    for (uint32_t i = 0; i < 64; ++i) {
        const uint16_t v = uint16_t(seed + i * 0x0841);
        std::memcpy(vram_data() + vram_offset64(base + 2 * i), &v, 2);
    }
}

} // namespace

BS_TEST(render_capture_strip_is_one_draw) {
    CaptureScene s;
    s.ta({0x80000002u, kIspAlways, kTspOpaque, 0});
    s.vtx(2, 2, 1, 0, 0, 0xFFFF0000);
    s.vtx(30, 2, 1, 0, 0, 0xFF00FF00);
    s.vtx(2, 30, 1, 0, 0, 0xFF0000FF);
    s.vtx(30, 30, 1, 0, 0, 0xFFFFFFFF, true);
    s.endList();
    s.render();
    const rc::Frame& f = rc::last_frame();
    BS_CHECK(f.cl.draws.size() == 1);
    BS_CHECK(f.cl.draws[0].vertex_count == 4);
    BS_CHECK(f.cl.draws[0].state.list == nr::ListType::Opaque);
    BS_CHECK(f.cl.draws[0].state.gouraud);
    BS_CHECK(f.cl.vertices[1].color == 0xFF00FF00);
    const auto tris = rc::triangles(f);
    BS_CHECK(tris.size() == 2);
    BS_CHECK(tris[1].v[0].x == 2 && tris[1].v[0].y == 30);  // k impar: (v2, v1, v3), os dois primeiros trocados
    BS_CHECK(tris[1].v[1].x == 30 && tris[1].v[1].y == 2 && tris[1].v[2].y == 30);
    BS_CHECK(f.cl.frame.clear_color == 0xFF102030);
    s.expectClean();
}

BS_TEST(render_capture_sprite_is_two_triangle_draws) {
    CaptureScene s;
    s.ta({0xA0000000u, kIspAlways, kTspOpaque, 0, 0xFF808080, 0});
    s.ta({0xF0000000u, fb(4), fb(4), fb(1), fb(28), fb(4), fb(1), fb(28)});
    s.ta({fb(28), fb(1), fb(4), fb(28), 0, 0, 0, 0});
    s.endList();
    s.render();
    const rc::Frame& f = rc::last_frame();
    BS_CHECK(f.sprites == 1);
    BS_CHECK(f.cl.draws.size() == 2);
    BS_CHECK(f.cl.draws[0].vertex_count == 3 && f.cl.draws[1].vertex_count == 3);
    BS_CHECK(f.cl.vertices[3].x == 4 && f.cl.vertices[4].x == 28 && f.cl.vertices[5].x == 4);  // (A, C, D)
    s.expectClean();
}

BS_TEST(render_capture_state_change_mid_strip_goes_per_triangle) {
    CaptureScene s;
    s.ta({0x80020002u, kIspAlways, kTspOpaque, 0});  // recorte do usuario: dentro
    s.ta({0x20000000u, 0, 0, 0, 0, 0, 0, 0});        // tile (0, 0)
    s.vtx(0, 0, 1, 0, 0, 0xFFFF0000);
    s.vtx(32, 0, 1, 0, 0, 0xFFFF0000);
    s.vtx(0, 32, 1, 0, 0, 0xFFFF0000);
    s.ta({0x20000000u, 0, 0, 0, 1, 1, 1, 1});        // muda o retangulo no meio do strip
    s.vtx(32, 32, 1, 0, 0, 0xFF00FF00);
    s.vtx(0, 40, 1, 0, 0, 0xFF00FF00, true);
    s.endList();
    s.render();
    const rc::Frame& f = rc::last_frame();
    BS_CHECK(f.cl.draws.size() == 3);
    for (const nr::Draw& d : f.cl.draws) BS_CHECK(d.vertex_count == 3 && d.state.clip == nr::Clip::Inside);
    BS_CHECK(f.cl.draws[0].state.clip_x1 == 31 && f.cl.draws[1].state.clip_x1 == 63);
    s.expectClean();
}

// Todos os bits do TSP fora do tamanho da textura (31-6) e os do ISP do estado (31-26) voltam iguais: cada campo
// isolado (um bit por vez) e combinacoes pseudoaleatorias (revisao Codex 2026-10-21: filtro 3, mip bias, SRC/DST)
BS_TEST(render_capture_state_round_trip) {
    std::vector<uint32_t> tsps = {kTspOpaque}, isps = {kIspAlways};
    for (uint32_t b = 6; b < 32; ++b) tsps.push_back(1u << b);
    for (uint32_t b = 26; b < 32; ++b) isps.push_back(1u << b);
    uint32_t x = 0x12345678;
    for (int i = 0; i < 64; ++i) {
        x = x * 1664525u + 1013904223u;
        tsps.push_back(x & ~0x3Fu);
        isps.push_back(x & 0xFC000000u);
    }
    for (uint32_t& tsp : tsps) {  // flip junto com clamp nao tem efeito (o clamp prevalece, como no rasterizador)
        if (tsp & (1u << 16)) tsp &= ~(1u << 18);
        if (tsp & (1u << 15)) tsp &= ~(1u << 17);
    }
    for (uint32_t tsp : tsps)
        for (uint32_t isp : isps) {
            PvrTri t;
            t.list = 2, t.isp = isp, t.tsp = tsp, t.gouraud = true, t.offset = true;
            t.clipMode = 3, t.cx0 = 32, t.cy0 = 0, t.cx1 = 63, t.cy1 = 31;
            const nr::DrawState d = rc::draw_state(t, 0);
            const PvrTri        b = rc::pvr_state(d, nullptr);
            BS_CHECK(b.tsp == tsp && b.isp == isp && b.list == 2 && b.gouraud && b.offset && b.clipMode == 3);
            BS_CHECK(b.cx1 == 63 && rc::draw_state(b, 0) == d);
            if (b.tsp != tsp || b.isp != isp) {
                std::printf("  tsp %08X -> %08X, isp %08X -> %08X\n", tsp, b.tsp, isp, b.isp);
                return;
            }
        }
    PvrTri t;
    t.tsp = 3u << 13;
    BS_CHECK(rc::draw_state(t, 0).filter == nr::Filter::TrilinearB);
    t.isp = 2u << 27;
    BS_CHECK(rc::draw_state(t, 0).cull == nr::Cull::CounterClockwise);
    t.isp = 3u << 27;
    BS_CHECK(rc::draw_state(t, 0).cull == nr::Cull::Clockwise);
    PvrVertex v{1.5f, -2.25f, 0.125f, 0.5f, 0.75f, {1, 0.2f, 0.4f, 0.6f}, {0, 0, 0, 0}};
    v.col = {200 / 255.f, 51 / 255.f, 102 / 255.f, 153 / 255.f};
    const PvrVertex w = rc::pvr_vertex(rc::vertex(v));
    BS_CHECK(std::memcmp(&v, &w, sizeof v) == 0);
}

BS_TEST(render_capture_modifier_volume) {
    CaptureScene s;
    s.ta({0x80000002u, kIspAlways, kTspOpaque, 0});
    s.vtx(2, 2, 1, 0, 0, 0xFFFF0000);
    s.vtx(30, 2, 1, 0, 0, 0xFFFF0000);
    s.vtx(2, 30, 1, 0, 0, 0xFFFF0000, true);
    s.endList();
    s.ta({0x81000000u, 1u << 29, 0, 0});  // volume modificador opaco, ultimo poligono "dentro"
    s.ta({0xF0000000u, fb(0), fb(0), fb(2), fb(32), fb(0), fb(2), fb(0)});
    s.ta({fb(32), fb(2), 0, 0, 0, 0, 0, 0});
    s.endList();
    s.render();
    const rc::Frame& f = rc::last_frame();
    BS_CHECK(f.cl.draws.size() == 1);
    BS_CHECK(f.cl.modifiers.size() == 1);
    BS_CHECK(f.modifier_volumes == 1 && f.modifier_params == 1);
    BS_CHECK(f.cl.modifiers[0].triangle_count == 1 && f.cl.modifiers[0].inclusion);
    BS_CHECK(f.cl.modifiers[0].list == nr::ListType::OpaqueModifier);
    BS_CHECK(f.cl.vertices[f.cl.modifiers[0].first_vertex + 1].x == 32);
    s.expectClean();
}

// Identidade por conteudo (contrato das texturas HD): a mesma textura em dois enderecos tem o mesmo id; outro
// conteudo, outro id. O handle da IR continua por chave (TCW), o id nao depende do endereco.
BS_TEST(render_capture_texture_content_id_is_address_independent) {
    CaptureScene s;
    loadTexture(kTexA, 0x1234);
    loadTexture(kTexB, 0x1234);
    const uint32_t tcwA = (1u << 27) | (kTexA >> 3), tcwB = (1u << 27) | (kTexB >> 3);
    for (uint32_t tcw : {tcwA, tcwB}) {
        s.ta({0x8000000Au, kIspAlways, kTspOpaque, tcw});
        s.vtx(0, 0, 1, fb(0), fb(0), 0xFFFFFFFF);
        s.vtx(32, 0, 1, fb(1), fb(0), 0xFFFFFFFF);
        s.vtx(0, 32, 1, fb(0), fb(1), 0xFFFFFFFF, true);
    }
    s.endList();
    s.render();
    const rc::Frame& f = rc::last_frame();
    BS_CHECK(f.textures.size() == 2);
    nr::TextureId a, b;
    BS_CHECK(rc::content_id(f.textures[0], a) && rc::content_id(f.textures[1], b));
    BS_CHECK(a == b && a.width == 8 && a.height == 8);
    BS_CHECK(nr::parse_texture_id(a.text()) == a);
    loadTexture(kTexB, 0x4321);
    BS_CHECK(rc::content_id(f.textures[1], b) && !(a == b));
    s.expectClean();
}

// Cull 2 descarta o anti-horario na tela (y para baixo) e 3 o horario: o nome no enum segue o que o rasterizador faz
BS_TEST(render_capture_cull_names_follow_the_rasterizer) {
    for (uint32_t cull : {2u, 3u}) {
        CaptureScene s;
        s.ta({0x80000002u, kIspAlways | (cull << 27), kTspOpaque, 0});
        s.vtx(2, 2, 1, 0, 0, 0xFFFF0000);
        s.vtx(30, 2, 1, 0, 0, 0xFFFF0000);
        s.vtx(2, 30, 1, 0, 0, 0xFFFF0000, true);  // horario na tela: (2,2) -> (30,2) -> (2,30)
        s.endList();
        s.render();
        const rc::Frame& f = rc::last_frame();
        BS_CHECK(f.cl.draws.size() == 1);
        uint16_t px;
        std::memcpy(&px, vram_data() + 0x100000 + 8 * 64 + 8 * 2, 2);
        const bool drawn = px == 0xF800;
        BS_CHECK(f.cl.draws[0].state.cull == (cull == 2 ? nr::Cull::CounterClockwise : nr::Cull::Clockwise));
        BS_CHECK(drawn == (cull == 2));  // horario: o modo 2 desenha, o 3 descarta
        s.expectClean();
    }
}

// Volumes modificadores fecham pela instrucao do global (1 inclusao, 2 exclusao); globais 0 iguais continuam o
// volume, e o global seguinte ao ultimo poligono comeca outro, mesmo com as palavras iguais
BS_TEST(render_capture_modifier_volumes_close_on_last_polygon) {
    CaptureScene s;
    auto tri = [&](float x) {
        s.ta({0xF0000000u, fb(x), fb(0), fb(2), fb(x + 8), fb(0), fb(2), fb(x)});
        s.ta({fb(8), fb(2), 0, 0, 0, 0, 0, 0});
    };
    s.ta({0x81000000u, 0, 0, 0});        // volume 1: poligonos normais...
    tri(0);
    s.ta({0x81000000u, 0, 0, 0});        // ...global igual: continua o volume
    tri(1);
    s.ta({0x81000000u, 1u << 29, 0, 0});  // ultimo poligono, inclusao
    tri(2);
    s.ta({0x81000000u, 1u << 29, 0, 0});  // global igual ao anterior: volume 2 (so o ultimo poligono)
    tri(3);
    s.ta({0x81000000u, 0, 0, 0});        // volume 3, exclusao
    tri(4);
    s.ta({0x81000000u, 2u << 29, 0, 0});
    tri(5);
    s.ta({0x81000000u, 0, 0, 0});        // volume 4 sem ultimo poligono
    tri(6);
    s.endList();
    s.render();
    const rc::Frame& f = rc::last_frame();
    BS_CHECK(f.cl.modifiers.size() == 4 && f.modifier_volumes == 4);
    if (f.cl.modifiers.size() != 4) return;
    BS_CHECK(f.cl.modifiers[0].triangle_count == 3 && f.cl.modifiers[0].inclusion);
    BS_CHECK(f.cl.modifiers[1].triangle_count == 1 && f.cl.modifiers[1].inclusion);
    BS_CHECK(f.cl.modifiers[2].triangle_count == 2 && !f.cl.modifiers[2].inclusion);
    BS_CHECK(f.cl.modifiers[3].triangle_count == 1);
    BS_CHECK(f.modifier_unclosed == 1);
    BS_CHECK(f.cl.vertices[f.cl.modifiers[1].first_vertex].x == 3);
    s.expectClean();
}

// A imagem a partir da IR so depende da IR: os registradores do quadro mudados depois do render nao a alteram, e os
// FrameParams reproduzem os registradores usados (revisao Codex 2026-10-21)
BS_TEST(render_capture_image_from_ir_ignores_live_registers) {
    CaptureScene s;
    s.reg(0x080, 7);             // HALF_OFFSET
    s.reg(0x11C, 0x40);          // PT_ALPHA_REF
    s.reg(0x0BC, 0xFFC0C0C0);    // FPU_CLAMP_MAX
    s.reg(0x0C0, 0x00101010);    // FPU_CLAMP_MIN
    s.reg(0x078, fb(1.5f));      // FPU_CULL_VAL
    s.ta({0x80000002u, kIspAlways, kTspOpaque | (1u << 21), 0});  // color clamp
    s.vtx(1.3f, 1.7f, 1, 0, 0, 0xFFFF8000);
    s.vtx(30.2f, 3.1f, 1, 0, 0, 0xFF00FFFF);
    s.vtx(4.4f, 29.9f, 1, 0, 0, 0xFF4080C0, true);
    s.endList();
    s.render();
    const rc::Frame& f   = rc::last_frame();
    const PvrFrame   fr  = rc::pvr_frame(f);
    BS_CHECK(fr == pvr_frame_from_regs());
    const auto       tri = rc::triangles(f);
    const auto       a   = pvr_render_triangles(tri, fr, 32, 32);
    s.reg(0x080, 0), s.reg(0x0BC, 0xFFFFFFFF), s.reg(0x0C0, 0), s.reg(0x088, fb(5.f)), s.reg(0x098, 1);
    BS_CHECK(!(pvr_frame_from_regs() == fr));
    BS_CHECK(pvr_render_triangles(tri, fr, 32, 32) == a);
    s.expectClean();
}
