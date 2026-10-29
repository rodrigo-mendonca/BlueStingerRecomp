// N8.7 (docs/N8-7-KAMUI.md): o ListSink nativo (native/render/kamui), sem o jogo e sem o hardware simulado: os blocos
// de 32 bytes da FIFO de poligonos, a juncao dos parametros de 64 bytes, o fim de lista e a IR decodificada.
#include "test.h"

#include "render/kamui/list_sink.h"

#include <cstring>

using namespace bs::native::render;
using kamui::ListFrame;
using kamui::ListSink;

namespace {

uint32_t fbits(float f) {
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}

// PCW: tipo (31-29), fim de strip (28), lista (26-24), controle do objeto (7-0)
uint32_t pcw(uint32_t type, uint32_t list, uint32_t obj, bool end = false) {
    return (type << 29) | (end ? 1u << 28 : 0) | (list << 24) | obj;
}

void feed(ListSink& s, std::initializer_list<uint32_t> w) {
    uint32_t b[8] = {};
    size_t   i    = 0;
    for (uint32_t x : w) b[i++] = x;
    s.feed(b);
}

// vertice empacotado (tipo 0): x, y, 1/w e a cor nas palavras 1..3 e 6
void vertex(ListSink& s, float x, float y, uint32_t argb, bool end, uint32_t list = 0) {
    feed(s, {pcw(7, list, 0, end), fbits(x), fbits(y), fbits(1.f), 0, 0, argb, 0});
}

FrameParams frame() {
    FrameParams f;
    f.width = 640, f.height = 480;
    return f;
}

} // namespace

BS_TEST(kamui_list_sink_strip_is_one_draw_and_list_end_reports_the_list) {
    ListSink s;
    s.list_init();
    feed(s, {pcw(4, 0, 0x02), 0x80000000u, 0x20000000u, 0, 0, 0, 0, 0});  // global opaco, gouraud
    vertex(s, 0, 0, 0xFFFF0000, false);
    vertex(s, 10, 0, 0xFF00FF00, false);
    vertex(s, 0, 10, 0xFF0000FF, false);
    vertex(s, 10, 10, 0xFFFFFFFF, true);
    uint32_t end[8] = {pcw(0, 0, 0)};
    const kamui::BlockResult r = s.feed(end);
    BS_CHECK(r.list_end == 0 && !r.invalid);
    BS_CHECK(s.bytes() == 6 * 32 && s.params().size() == 6);
    ListFrame f;
    s.build(frame(), 0, f);
    BS_CHECK(f.cl.draws.size() == 1 && f.cl.draws[0].vertex_count == 4 && f.cl.vertices.size() == 4);
    BS_CHECK(f.triangles == 2 && f.cl.draws[0].state.list == ListType::Opaque && f.cl.draws[0].state.gouraud);
    BS_CHECK(f.cl.vertices[1].x == 10.f && f.cl.vertices[1].color == 0xFF00FF00u);
}

BS_TEST(kamui_list_sink_joins_64_byte_parameters_and_rejects_invalid_type) {
    ListSink s;
    s.list_init();
    // global de intensidade com textura e offset (64 bytes): a segunda metade nao e um parametro
    feed(s, {pcw(4, 2, 0x2C), 0, 0, 0, 0, 0, 0, 0});
    BS_CHECK(s.params().empty());
    feed(s, {fbits(1.f), fbits(1.f), fbits(1.f), fbits(1.f), fbits(0.f), 0, 0, 0});
    BS_CHECK(s.params().size() == 1 && s.params()[0].words == 16 && s.params()[0].list == 2);
    uint32_t bad[8] = {3u << 29};  // tipo 3 nao existe
    BS_CHECK(s.feed(bad).invalid);
    s.list_init();
    BS_CHECK(s.params().empty() && s.bytes() == 0);
}

BS_TEST(kamui_list_sink_sprite_is_two_triangles_and_texture_table_dedups) {
    ListSink s;
    s.list_init();
    const uint32_t tcw = 0x08000100;  // 565 twiddled
    const uint32_t tsp = (1u << 3) | 1u;  // 16 x 16
    feed(s, {pcw(5, 2, 0x08), 0, tsp, tcw, 0xFFFFFFFF, 0, 0, 0});  // sprite com textura
    for (int k = 0; k < 2; ++k) {
        // sprite: 64 bytes; A (0,0), B (8,0), C (8,8) com z nas palavras 1..9, D (0,8) sem z nas 10..11, UV de 16 bits
        // nas 13..15
        feed(s, {pcw(7, 2, 0, true), fbits(0), fbits(0), fbits(1), fbits(8), fbits(0), fbits(1), fbits(8)});
        feed(s, {fbits(8), fbits(1), fbits(0), fbits(8), 0, 0, 0, 0});
    }
    ListFrame f;
    s.build(frame(), 64, f);
    BS_CHECK(f.sprites == 2 && f.cl.draws.size() == 4 && f.textures.size() == 1);
    BS_CHECK(f.textures[0].tcw == tcw && f.textures[0].width == 16 && f.textures[0].height == 16 && f.textures[0].stride == 64);
    BS_CHECK(f.cl.draws[0].state.texture.value == 1 && f.cl.draws[3].state.texture.value == 1);
}

BS_TEST(kamui_list_sink_modifier_volume_closes_by_the_global_instruction) {
    ListSink s;
    s.list_init();
    auto tri = [&](bool end) {
        feed(s, {pcw(7, 1, 0, end), fbits(0), fbits(0), fbits(2), fbits(9), fbits(0), fbits(2), fbits(0)});
        feed(s, {fbits(9), fbits(2), 0, 0, 0, 0, 0, 0});
    };
    feed(s, {pcw(4, 1, 0x40), 0u << 29, 0, 0, 0, 0, 0, 0});  // volume: instrucao 0 (poligono do volume)
    tri(false);
    feed(s, {pcw(4, 1, 0x40), 1u << 29, 0, 0, 0, 0, 0, 0});  // instrucao 1: ultimo poligono, inclusao
    tri(true);
    ListFrame f;
    s.build(frame(), 0, f);
    BS_CHECK(f.cl.modifiers.size() == 1 && f.cl.modifiers[0].triangle_count == 2 && f.cl.modifiers[0].closed);
    BS_CHECK(f.cl.modifiers[0].inclusion && f.cl.modifiers[0].or_from == ~0u && f.volume_inclusion == 1);
    BS_CHECK(f.modifier_params == 2 && f.modifier_unclosed == 0 && f.cl.draws.empty());
}

BS_TEST(kamui_list_sink_unsupported_texture_is_counted_not_drawn) {
    ListSink s;
    s.list_init();
    feed(s, {pcw(4, 0, 0x08), 0, 0, (4u << 27), 0, 0, 0, 0});  // textura bump (formato 4): sem suporte
    vertex(s, 0, 0, 0, false);
    vertex(s, 1, 0, 0, false);
    vertex(s, 0, 1, 0, true);
    ListFrame f;
    s.build(frame(), 0, f);
    BS_CHECK(f.cl.draws.empty() && f.unsupported == 3 && f.skipped_params == 3);
}
