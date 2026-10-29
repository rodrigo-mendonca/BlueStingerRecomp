// N7a: captura da IR de render (transitional/render_capture) a partir do decodificador do TA, em cenas sinteticas
// pelo caminho do jogo (registradores, FIFO do TA, STARTRENDER). O modo conferir prova em cada render que os
// triangulos da IR sao os desenhados e que a imagem redesenhada a partir dela e identica.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "pvr/pvr_regs.h"
#include "pvr/ta.h"
#include "pvr/vram.h"
#include "render/vulkan/vk_device.h"
#include "render_capture.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

    explicit CaptureScene(rc::Mode mode = rc::Mode::Check) {
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
        rc::install(mode);
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
        vram_touch(vram_offset64(base + 2 * i), 2);  // N8.7 3b: quem escreve por vram_data() marca a pagina
    }
}

} // namespace

// N8.7 3b (docs/N8-7-KAMUI.md): o cache de texturas usa a geracao da memoria das texturas; uma textura regravada pela CPU
// (o caminho de 64 bits, como a copia do Kamui) entre dois renders aparece no segundo. Nos dois builds; na validacao, os
// bytes relidos confirmam que nenhuma textura ficou velha. Com BS_TEXTURE_HEAP_MUTACAO=marca (validacao: as escritas nao
// marcam a pagina) este teste tem de falhar
BS_TEST(render_capture_texture_cache_sees_a_rewritten_texture) {
    CaptureScene s(rc::Mode::Software);
    auto fill = [&](uint16_t c565) {  // 8x8 RGB565 numa cor, pela escrita do jogo (0xA4000000 = caminho de 64 bits)
        for (uint32_t i = 0; i < 64; ++i) sh4_write16(&s.c, 0xA4000000 + kTexA + 2 * i, c565);
    };
    auto draw = [&] {
        s.reg(0x144, 0x80000000);  // TA_LIST_INIT
        s.ta({0x8000000Au, kIspAlways, kTspOpaque, (1u << 27) | (kTexA >> 3)});  // 565, twiddled, decal
        s.vtx(0, 0, 1, fb(0), fb(0), 0xFFFFFFFF);
        s.vtx(64, 0, 1, fb(1), fb(0), 0xFFFFFFFF);
        s.vtx(0, 64, 1, fb(0), fb(1), 0xFFFFFFFF, true);
        s.endList();
        s.render();
    };
    auto px = [] {
        uint16_t v;
        std::memcpy(&v, vram_data() + 0x100000 + 5 * 64 + 5 * 2, 2);
        return v;
    };
    fill(0xF800);
    draw();
    BS_CHECK(px() == 0xF800);
    fill(0x07E0);
    draw();
    BS_CHECK(px() == 0x07E0);
    BS_CHECK(rc::stats().tex_stale == s.before.tex_stale);
}

// a mesma textura regravada pelo caminho direto de textura do TA (o destino das store queues e do DMA do canal 2), e uma
// textura de paleta de 4 bits que so muda a paleta: o cache ve as duas mudancas
BS_TEST(render_capture_texture_cache_sees_ta_path_and_palette_changes) {
    CaptureScene s(rc::Mode::Software);
    auto draw = [&](uint32_t tcw) {
        s.reg(0x144, 0x80000000);
        s.ta({0x8000000Au, kIspAlways, kTspOpaque, tcw});
        s.vtx(0, 0, 1, fb(0), fb(0), 0xFFFFFFFF);
        s.vtx(64, 0, 1, fb(1), fb(0), 0xFFFFFFFF);
        s.vtx(0, 64, 1, fb(0), fb(1), 0xFFFFFFFF, true);
        s.endList();
        s.render();
    };
    auto px = [] {
        uint16_t v;
        std::memcpy(&v, vram_data() + 0x100000 + 5 * 64 + 5 * 2, 2);
        return v;
    };
    auto ta_fill = [&](uint16_t c565) {  // 128 bytes (8x8 x 16 bits) em blocos de 32 pelo caminho direto de textura
        for (uint32_t off = 0; off < 128; off += 32) {
            uint32_t d[8];
            for (uint32_t& w : d) w = uint32_t(c565) | (uint32_t(c565) << 16);
            BS_CHECK(ta_fifo_write(&s.c, 0x11000000 + kTexA + off, d));
        }
    };
    const uint32_t tcw565 = (1u << 27) | (kTexA >> 3);
    ta_fill(0xF800);
    draw(tcw565);
    BS_CHECK(px() == 0xF800);
    ta_fill(0x001F);
    draw(tcw565);
    BS_CHECK(px() == 0x001F);
    // paleta de 4 bits (formato 5, seletor 0): indices 0; a entrada 0 da paleta (565) muda entre os renders
    for (uint32_t i = 0; i < 32; ++i) sh4_write8(&s.c, 0xA4000000 + kTexB + i, 0);
    s.reg(0x108, 1);  // PAL_RAM_CTRL: RGB565
    s.reg(0x1000, 0xF800);
    const uint32_t tcwPal = (5u << 27) | (kTexB >> 3);
    draw(tcwPal);
    BS_CHECK(px() == 0xF800);
    s.reg(0x1000, 0x07E0);
    draw(tcwPal);
    BS_CHECK(px() == 0x07E0);
    BS_CHECK(rc::stats().tex_stale == s.before.tex_stale);
}

#if BS_VALIDACAO  // o modo conferir: a IR contra o caminho do TA (N8.7: so na validacao)

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
    const auto       a   = pvr_render_triangles(tri, {}, {}, fr, 32, 32);
    s.reg(0x080, 0), s.reg(0x0BC, 0xFFFFFFFF), s.reg(0x0C0, 0), s.reg(0x088, fb(5.f)), s.reg(0x098, 1);
    BS_CHECK(!(pvr_frame_from_regs() == fr));
    BS_CHECK(pvr_render_triangles(tri, {}, {}, fr, 32, 32) == a);
    s.expectClean();
}
#endif  // BS_VALIDACAO

// N7b: --render nativo pelo caminho do jogo (registradores, FIFO do TA, STARTRENDER): a imagem do Vulkan vai para o
// framebuffer. Texturas HD pelo contrato do N7a: a substituicao compativel do mesmo TextureId e usada; uma
// incompativel (outra proporcao) fica no original; sem provedor, o original. Sem Vulkan: [skip].
BS_TEST(render_capture_native_vulkan_and_hd_replacement) {
    {
        std::string err;
        if (!nr::VulkanDevice::create(&err)) BS_SKIP("dispositivo Vulkan");
    }
    struct Provider : nr::ITextureReplacements {
        nr::TextureId          want;
        nr::ReplacementTexture tex;
        const nr::ReplacementTexture* find(const nr::TextureId& id) override { return id == want ? &tex : nullptr; }
    };
    auto frame = [](CaptureScene& s) {
        for (uint32_t i = 0; i < 64; ++i) {  // 8x8 RGB565 vermelho
            const uint16_t red = 0xF800;
            std::memcpy(vram_data() + vram_offset64(kTexA + 2 * i), &red, 2);
        }
        s.ta({0x8000000Au, kIspAlways, kTspOpaque, (1u << 27) | (kTexA >> 3)});  // decal: a cor e o texel
        s.vtx(0, 0, 1, fb(0), fb(0), 0xFFFFFFFF);
        s.vtx(32, 0, 1, fb(1), fb(0), 0xFFFFFFFF);
        s.vtx(0, 32, 1, fb(0), fb(1), 0xFFFFFFFF);
        s.vtx(32, 32, 1, fb(1), fb(1), 0xFFFFFFFF, true);
        s.endList();
        s.render();
        uint16_t px;
        std::memcpy(&px, vram_data() + 0x100000 + 16 * 64 + 16 * 2, 2);
        return px;
    };
    Provider p;
    p.want = nr::texture_id(8, 8, std::vector<uint32_t>(64, 0xFFFF0000u));
    {
        CaptureScene s(rc::Mode::Native);
        BS_CHECK(frame(s) == 0xF800);  // sem provedor: o original, desenhado pelo Vulkan
        BS_CHECK(rc::stats().vk_frames == 1 && rc::stats().vk_failures == 0);
    }
    p.tex = {32, 32, std::vector<uint32_t>(32 * 32, 0xFF00FF00u)};  // 4x, verde
    rc::set_texture_replacements(&p);
    {
        CaptureScene s(rc::Mode::Native);
        BS_CHECK(frame(s) == 0x07E0);
        BS_CHECK(rc::stats().tex_replaced == 1);
    }
    p.tex = {32, 16, std::vector<uint32_t>(32 * 16, 0xFF00FF00u)};  // outra proporcao: incompativel
    {
        CaptureScene s(rc::Mode::Native);
        BS_CHECK(frame(s) == 0xF800);
        BS_CHECK(rc::stats().tex_replace_rejected == 1 && rc::stats().tex_replaced == 0);
    }
    rc::set_texture_replacements(nullptr);
}
#if BS_VALIDACAO  // o modo comparar: o rasterizador do caminho do TA (N8.7: so na validacao)

// N7b (revisao Codex 2026-10-21): a IMAGEM INTEIRA do Vulkan contra a do software (BS_RENDER_IR=comparar) em cenas
// pelo caminho do jogo: gouraud, sprite, recorte, textura bilinear com repeticao e espelho, HALF_OFFSET 1 e 3,
// 1/w negativo (Always com escrita, depois Less e Equal), e translucidas em autosort alem da capacidade inicial da
// lista por pixel (o quadro e refeito). Criterio: PSNR >= 40 dB (docs/N7-PLANO.md secao 5) e no maximo 1% dos
// pixels com mais de 8 niveis de diferenca (bordas: a GPU encaixa os vertices em 1/256 de pixel). Sem Vulkan: [skip].
BS_TEST(render_capture_vulkan_full_image_matches_software) {
    {
        std::string err;
        if (!nr::VulkanDevice::create(&err)) BS_SKIP("dispositivo Vulkan");
    }
    struct Case {
        const char* name;
        void (*draw)(CaptureScene&);
    };
    const Case cases[] = {
        {"gouraud", [](CaptureScene& s) {
             s.ta({0x80000002u, kIspAlways, kTspOpaque, 0});
             s.vtx(1.3f, 0.7f, 1, 0, 0, 0xFFFF0000);
             s.vtx(31.2f, 3.4f, 0.5f, 0, 0, 0xFF00FF00);
             s.vtx(2.6f, 30.9f, 2, 0, 0, 0xFF0000FF);
             s.vtx(29.7f, 29.1f, 1.5f, 0, 0, 0xFFFFFFFF, true);
             s.endList();
         }},
        {"fatias_finas", [](CaptureScene& s) {  // o caso dos quadros < 40 dB da rota: arestas quase horizontais
             s.ta({0x80000002u, kIspAlways, kTspOpaque, 0});
             for (int i = 0; i < 8; ++i) {
                 const float y = 2.5f + 3.7f * float(i), e = 0.013f * float(i + 1);
                 s.vtx(0.31f, y - e, 1, 0, 0, 0xFFFF2010);
                 s.vtx(31.77f, y + 2 * e, 1, 0, 0, 0xFFFF2010);
                 s.vtx(0.31f, y + 0.61f, 1, 0, 0, 0xFFFF2010, true);
             }
             s.endList();
         }},
        {"sprite", [](CaptureScene& s) {
             s.ta({0xA0000000u, kIspAlways, kTspOpaque, 0, 0xFF808040, 0});
             s.ta({0xF0000000u, fb(4.5f), fb(3.25f), fb(1), fb(27.75f), fb(5), fb(1), fb(26)});
             s.ta({fb(28.5f), fb(1), fb(3), fb(27), 0, 0, 0, 0});
             s.endList();
         }},
        {"recorte", [](CaptureScene& s) {
             s.ta({0x80030002u, kIspAlways, kTspOpaque, 0});  // fora do tile (0, 0)
             s.ta({0x20000000u, 0, 0, 0, 0, 0, 0, 0});
             s.vtx(0, 0, 1, 0, 0, 0xFF20C040);
             s.vtx(32, 0, 1, 0, 0, 0xFF20C040);
             s.vtx(0, 32, 1, 0, 0, 0xFF20C040, true);
             s.endList();
         }},
        {"textura_bilinear", [](CaptureScene& s) {
             loadTexture(kTexA, 0x1234);
             // bilinear (TSP 14-13 = 1), U espelhado (18), V repetido; decal; UV de -0.7 a 2.3
             s.ta({0x8000000Au, kIspAlways, kTspOpaque | (1u << 13) | (1u << 18), (1u << 27) | (kTexA >> 3)});
             s.vtx(0, 0, 1, fb(-0.7f), fb(-0.7f), 0xFFFFFFFF);
             s.vtx(32, 0, 1, fb(2.3f), fb(-0.7f), 0xFFFFFFFF);
             s.vtx(0, 32, 1, fb(-0.7f), fb(2.3f), 0xFFFFFFFF);
             s.vtx(32, 32, 1, fb(2.3f), fb(2.3f), 0xFFFFFFFF, true);
             s.endList();
         }},
        {"half_offset_1", [](CaptureScene& s) {
             s.reg(0x080, 1);
             s.ta({0x80000002u, kIspAlways, kTspOpaque, 0});
             s.vtx(0, -32, 1, 0, 0, 0xFF000000);
             s.vtx(32, -32, 1, 0, 0, 0xFFFF0000);
             s.vtx(0, 96, 1, 0, 0, 0xFF000000);
             s.vtx(32, 96, 1, 0, 0, 0xFFFF0000, true);
             s.endList();
         }},
        {"half_offset_3", [](CaptureScene& s) {
             s.reg(0x080, 3);
             s.ta({0x80000002u, kIspAlways, kTspOpaque, 0});
             s.vtx(0, -32, 1, 0, 0, 0xFF000000);
             s.vtx(32, -32, 1, 0, 0, 0xFFFF0000);
             s.vtx(0, 96, 1, 0, 0, 0xFF000000);
             s.vtx(32, 96, 1, 0, 0, 0xFFFF0000, true);
             s.endList();
         }},
        {"profundidade_negativa", [](CaptureScene& s) {
             auto quad = [&](uint32_t isp, float z, uint32_t c) {
                 s.ta({0x80000002u, isp, kTspOpaque, 0});
                 s.vtx(0, 0, z, 0, 0, c);
                 s.vtx(32, 0, z, 0, 0, c);
                 s.vtx(0, 32, z, 0, 0, c);
                 s.vtx(32, 32, z, 0, 0, c, true);
             };
             quad(kIspAlways, -0.5f, 0xFFFF0000);  // escreve -0.5
             quad(1u << 29, -1.0f, 0xFF00FF00);    // Less: -1 < -0.5, escreve -1
             quad(2u << 29, -1.0f, 0xFF0000FF);    // Equal: -1 == -1
             s.endList();
         }},
        {"profundidade_extrema", [](CaptureScene& s) {  // o contraexemplo da revisao Codex 2026-10-22
             auto quad = [&](uint32_t isp, float z, uint32_t c) {
                 s.ta({0x80000002u, isp, kTspOpaque, 0});
                 s.vtx(0, 0, z, 0, 0, c);
                 s.vtx(32, 0, z, 0, 0, c);
                 s.vtx(0, 32, z, 0, 0, c);
                 s.vtx(32, 32, z, 0, 0, c, true);
             };
             // z - (-16777216) junta 0 e 1 em float; com a profundidade crua, 1 > 0 continua verdadeiro
             s.ta({0x80000002u, kIspAlways, kTspOpaque, 0});
             s.vtx(0, 0, -16777216.f, 0, 0, 0xFF808080);
             s.vtx(4, 0, -16777216.f, 0, 0, 0xFF808080);
             s.vtx(0, 4, -16777216.f, 0, 0, 0xFF808080, true);
             quad(kIspAlways, 0.0f, 0xFFFF0000);  // escreve 0
             quad(4u << 29, 1.0f, 0xFF00FF00);    // Greater: 1 > 0
             s.endList();
         }},
        {"autosort_70_camadas", [](CaptureScene& s) {  // 70 camadas: a ordenacao local do resolve
             for (int i = 0; i < 70; ++i) {
                 const float    z = 0.5f + float((i * 37) % 70) * 0.01f;  // profundidades fora de ordem, todas diferentes
                 const uint32_t c = (0x10u + uint32_t(i)) << 24 | uint32_t(i * 0x030507) & 0xFFFFFF;
                 s.ta({0x82000002u, kIspAlways, (4u << 29) | (5u << 26) | (1u << 20) | 0x00800000u, 0});
                 s.vtx(0, 0, z, 0, 0, c);
                 s.vtx(32, 0, z, 0, 0, c);
                 s.vtx(0, 32, z, 0, 0, c);
                 s.vtx(32, 32, z, 0, 0, c, true);
             }
             s.endList();
         }},
        {"autosort_140_camadas", [](CaptureScene& s) {  // acima de 128 camadas: o caminho da selecao repetida
             for (int i = 0; i < 140; ++i) {
                 const float    z = 0.5f + float((i * 37) % 140) * 0.005f;  // profundidades fora de ordem, todas diferentes
                 const uint32_t c = (0x08u + uint32_t(i)) << 24 | uint32_t(i * 0x030507) & 0xFFFFFF;
                 s.ta({0x82000002u, kIspAlways, (4u << 29) | (5u << 26) | (1u << 20) | 0x00800000u, 0});
                 s.vtx(0, 0, z, 0, 0, c);
                 s.vtx(32, 0, z, 0, 0, c);
                 s.vtx(0, 32, z, 0, 0, c);
                 s.vtx(32, 32, z, 0, 0, c, true);
             }
             s.endList();
         }},
        {"autosort_camadas", [](CaptureScene& s) {
             s.ta({0x80000002u, kIspAlways, kTspOpaque, 0});
             s.vtx(0, 0, 0.5f, 0, 0, 0xFF202020);
             s.vtx(32, 0, 0.5f, 0, 0, 0xFF202020);
             s.vtx(0, 32, 0.5f, 0, 0, 0xFF202020);
             s.vtx(32, 32, 0.5f, 0, 0, 0xFF202020, true);
             s.endList();
             // 6 camadas translucidas de tela inteira (SrcAlpha/InvSrcAlpha, use alpha) em profundidades fora de
             // ordem: 6 x 1024 fragmentos > 4 x 1024 (a capacidade inicial do alvo 32x32)
             const float    zs[6] = {1.5f, 0.8f, 2.5f, 1.1f, 3.0f, 0.9f};
             const uint32_t cs[6] = {0x80FF0000, 0x6000FF00, 0x400000FF, 0x90FFFF00, 0x50FF00FF, 0x7000FFFF};
             for (int i = 0; i < 6; ++i) {
                 s.ta({0x82000002u, kIspAlways, (4u << 29) | (5u << 26) | (1u << 20) | 0x00800000u, 0});
                 s.vtx(0, 0, zs[i], 0, 0, cs[i]);
                 s.vtx(32, 0, zs[i], 0, 0, cs[i]);
                 s.vtx(0, 32, zs[i], 0, 0, cs[i]);
                 s.vtx(32, 32, zs[i], 0, 0, cs[i], true);
             }
             s.endList();
         }},
    };
    for (const Case& c : cases) {
        // diagnostico: BS_TESTE_BMP=<pasta> grava software | Vulkan | diferenca de cada cena
        if (const char* d = std::getenv("BS_TESTE_BMP")) rc::set_compare_dump(std::string(d) + "/" + c.name, 1);
        CaptureScene s(rc::Mode::Compare);
        c.draw(s);
        s.render();
        const rc::Stats& st   = rc::stats();
        const double     frac = st.cmp_pixels ? double(st.cmp_diff8) / double(st.cmp_pixels) : 1.0;
        std::fprintf(stderr, "  %-22s PSNR %5.1f dB, pixels diferentes %llu, > 8 niveis %llu, maior %u\n", c.name,
                     st.cmp_min_psnr, static_cast<unsigned long long>(st.cmp_diff), static_cast<unsigned long long>(st.cmp_diff8),
                     st.cmp_max);
        BS_CHECK(st.vk_frames == 1 && st.vk_failures == 0 && st.cmp_frames == 1);
        // modo fiel: imagem IDENTICA (revisao Codex 2026-10-22); PSNR e fracao ficam para os modos de melhoria
        BS_CHECK(st.cmp_diff == 0 && st.cmp_max == 0);
        (void)frac;
        if (std::string(c.name) == "autosort_camadas") BS_CHECK(st.vk_fragment_retries >= 1);
        if (std::string(c.name) == "profundidade_negativa") BS_CHECK(st.vk_negative_depth == 1);
    }
}
#endif  // BS_VALIDACAO

// Recuo para o software (revisao Codex 2026-10-21): textura que nao chega ao Vulkan = este render no software
// (nunca um desenho sem textura); falha do dispositivo = o Vulkan sai e o resto da sessao fica no software.
BS_TEST(render_capture_vulkan_failures_fall_back_to_software) {
    {
        std::string err;
        if (!nr::VulkanDevice::create(&err)) BS_SKIP("dispositivo Vulkan");
    }
    auto textured = [](CaptureScene& s) {
        for (uint32_t i = 0; i < 64; ++i) {
            const uint16_t red = 0xF800;
            std::memcpy(vram_data() + vram_offset64(kTexA + 2 * i), &red, 2);
        }
        s.ta({0x8000000Au, kIspAlways, kTspOpaque, (1u << 27) | (kTexA >> 3)});
        s.vtx(0, 0, 1, fb(0), fb(0), 0xFFFFFFFF);
        s.vtx(32, 0, 1, fb(1), fb(0), 0xFFFFFFFF);
        s.vtx(0, 32, 1, fb(0), fb(1), 0xFFFFFFFF);
        s.vtx(32, 32, 1, fb(1), fb(1), 0xFFFFFFFF, true);
        s.endList();
        s.render();
        uint16_t px;
        std::memcpy(&px, vram_data() + 0x100000 + 16 * 64 + 16 * 2, 2);
        return px;
    };
    {
        CaptureScene s(rc::Mode::Native);
        rc::inject_fault(rc::Fault::Texture);
        BS_CHECK(textured(s) == 0xF800);  // o software desenhou (com a textura)
        BS_CHECK(rc::stats().vk_frames == 0 && rc::stats().vk_failures == 1);
        s.reg(0x144, 0x80000000);
        BS_CHECK(textured(s) == 0xF800 && rc::stats().vk_frames == 1);  // o render seguinte volta ao Vulkan
    }
    {  // perda transitoria: o dispositivo e recriado e o mesmo render sai pelo Vulkan
        CaptureScene s(rc::Mode::Native);
        rc::inject_fault(rc::Fault::DeviceOnce);
        BS_CHECK(textured(s) == 0xF800);
        BS_CHECK(rc::stats().vk_recoveries == 1 && rc::stats().vk_frames == 1 && rc::stats().vk_error.empty());
    }
    {  // perda que se repete na recuperacao: software ate o fim da sessao
        CaptureScene s(rc::Mode::Native);
        rc::inject_fault(rc::Fault::Device);
        BS_CHECK(textured(s) == 0xF800);
        BS_CHECK(rc::stats().vk_failures == 2 && rc::stats().vk_error.find("dispositivo") != std::string::npos);
        s.reg(0x144, 0x80000000);
        BS_CHECK(textured(s) == 0xF800 && rc::stats().vk_frames == 0);  // sem Vulkan ate o fim da sessao
    }
    rc::inject_fault(rc::Fault::None);
}

// Diagnostico (nao roda sem a variavel): BS_RENDER_IR_REPRODUZIR=<arquivo de BS_RENDER_IR_GUARDAR> reproduz o render
// gravado no dispositivo de BS_VULKAN_GPU, BS_RENDER_IR_VEZES vezes (padrao 3). Se o dispositivo falhar, procura por
// bissecao o menor prefixo de desenhos que ainda falha (um dispositivo novo por tentativa) e mostra o desenho culpado.
BS_TEST(render_vulkan_replay_dump) {
    const char* path = std::getenv("BS_RENDER_IR_REPRODUZIR");
    if (!path || !*path) return;
    nr::CommandList              cl;
    std::vector<rc::DumpTexture> tex;
    BS_CHECK(rc::load_frame_dump(path, cl, tex));
    std::fprintf(stderr, "  IR: %zu desenhos, %zu vertices, %zu texturas, %ux%u\n", cl.draws.size(), cl.vertices.size(),
                 tex.size(), cl.frame.width, cl.frame.height);
    auto attempt = [&](size_t draws, int times, std::string* why) {
        std::string err;
        auto        d = nr::VulkanDevice::create(&err);
        if (!d) {
            *why = "create: " + err;
            return false;
        }
        std::vector<nr::TextureHandle> h;
        for (const auto& t : tex) {
            auto r = d->create_texture(t.desc, std::as_bytes(std::span<const uint32_t>(t.argb)));
            if (!r) {
                *why = "textura";
                return false;
            }
            h.push_back(r.value());
        }
        nr::CommandList c = cl;
        c.draws.resize(draws);
        for (auto& dr : c.draws)
            if (dr.state.texture) dr.state.texture = h[dr.state.texture.value - 1];
        uint64_t hash = 0;
        for (int i = 0; i < times; ++i) {
            std::vector<uint32_t> img;
            uint32_t              w = 0, hh = 0;
            if (!d->submit(c).ok() || !d->read_frame(img, w, hh).ok()) {
                *why = d->stats().last_error;
                return false;
            }
            uint64_t hv = 0xCBF29CE484222325ull;
            for (uint32_t px : img) hv = (hv ^ px) * 0x100000001B3ull;
            if (i && hv != hash) std::fprintf(stderr, "  imagem diferente entre repeticoes (%d)\n", i);
            hash = hv;
        }
        std::fprintf(stderr, "  %zu desenhos x %d: ok (GPU pior %.1f ms, camadas %llu), imagem %016llX\n", draws, times,
                     d->stats().gpu_ms_max, static_cast<unsigned long long>(d->stats().max_layers),
                     static_cast<unsigned long long>(hash));
        return true;
    };
    const char* v     = std::getenv("BS_RENDER_IR_VEZES");
    const int   times = v ? std::max(1, std::atoi(v)) : 3;
    std::string why;
    if (attempt(cl.draws.size(), times, &why)) return;
    std::fprintf(stderr, "  falhou com todos os desenhos: %s\n", why.c_str());
    size_t lo = 0, hi = cl.draws.size();  // lo passa, hi falha
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (attempt(mid, times, &why)) lo = mid;
        else hi = mid;
    }
    const nr::Draw& d = cl.draws[hi - 1];
    std::fprintf(stderr, "  o desenho %zu derruba o dispositivo: lista %u, %u vertices a partir de %u, textura %u, recorte %u\n",
                 hi - 1, unsigned(d.state.list), d.vertex_count, d.first_vertex, d.state.texture.value, unsigned(d.state.clip));
    for (uint32_t i = 0; i < std::min<uint32_t>(d.vertex_count, 6); ++i) {
        const nr::Vertex& x = cl.vertices[d.first_vertex + i];
        std::fprintf(stderr, "    v%u: x %g y %g 1/w %g u %g v %g cor %08X\n", i, x.x, x.y, x.inv_w, x.u, x.v, x.color);
    }
    BS_CHECK(false);
}

#if BS_VALIDACAO
// N8.7 fase 0 (revisao Codex 20261028-173027): a sonda grafica encadeia a substituicao que ja existia no tratador do Kamui
// e a devolve ao desligar; uma substituicao instalada depois dela nao e desfeita pelo desligamento
#include "cpu/sh4_runtime.h"
#include "debug/gfx_probe.h"

namespace {
int  g_fakeIsrHits = 0;
void fake_isr(Sh4Context*) { ++g_fakeIsrHits; }
} // namespace

BS_TEST(gfx_probe_chains_and_restores_the_isr_override) {
    constexpr uint32_t isr  = 0x8C1C53E2;
    const std::string  path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/bs_gfx_probe_teste.txt";
    // 1) o mod antes da sonda: a sonda o chama e o devolve
    g_fakeIsrHits = 0;
    sh4_override(isr, fake_isr);
    gfx_probe_init(path);
    BS_CHECK(sh4_override_get(isr) != fake_isr && sh4_override_get(isr) != nullptr);
    Sh4Context c{};
    sh4_lookup(isr)(&c);
    BS_CHECK(g_fakeIsrHits == 1);
    gfx_probe_init("");
    BS_CHECK(sh4_override_get(isr) == fake_isr);
    // 2) o mod depois da sonda: ele vence, e desligar a sonda nao o remove
    sh4_override(isr, nullptr);
    gfx_probe_init(path);
    sh4_override(isr, fake_isr);
    gfx_probe_init("");
    BS_CHECK(sh4_override_get(isr) == fake_isr);
    sh4_override(isr, nullptr);
    std::remove(path.c_str());
}
#endif

#if BS_VALIDACAO
// revisao Codex 20261029-013355: um envio de textura que falha nao confirma a geracao nova; o render seguinte relê os bytes
// e envia de novo (sem a correcao, o handle antigo seria reaproveitado com a textura velha). Na validacao o render que
// falhou no software sobre a IR cai no rasterizador do caminho do TA
BS_TEST(render_capture_failed_texture_upload_does_not_confirm_the_generation) {
    CaptureScene s(rc::Mode::Software);
    auto fill = [&](uint16_t c565) {
        for (uint32_t i = 0; i < 64; ++i) sh4_write16(&s.c, 0xA4000000 + kTexA + 2 * i, c565);
    };
    auto draw = [&] {
        s.reg(0x144, 0x80000000);
        s.ta({0x8000000Au, kIspAlways, kTspOpaque, (1u << 27) | (kTexA >> 3)});
        s.vtx(0, 0, 1, fb(0), fb(0), 0xFFFFFFFF);
        s.vtx(64, 0, 1, fb(1), fb(0), 0xFFFFFFFF);
        s.vtx(0, 64, 1, fb(0), fb(1), 0xFFFFFFFF, true);
        s.endList();
        s.render();
    };
    auto px = [] {
        uint16_t v;
        std::memcpy(&v, vram_data() + 0x100000 + 5 * 64 + 5 * 2, 2);
        return v;
    };
    fill(0xF800);
    draw();
    BS_CHECK(px() == 0xF800);
    fill(0x07E0);
    rc::inject_fault(rc::Fault::TextureUpload);
    const uint64_t failures = rc::stats().soft_failures;
    draw();  // o envio falha: o software sobre a IR cai, o caminho do TA desenha
    BS_CHECK(rc::stats().soft_failures == failures + 1 && px() == 0x07E0);
    draw();  // de novo pelo software: a textura tem de ser reenviada
    BS_CHECK(rc::stats().soft_failures == failures + 1 && px() == 0x07E0);
    BS_CHECK(rc::stats().tex_stale == s.before.tex_stale);
}
#endif
