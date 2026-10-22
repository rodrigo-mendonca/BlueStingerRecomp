// N7b: preparacao do quadro para a GPU (native/render/frame_prep): as decisoes por triangulo do rasterizador de
// referencia (runtime/pvr/pvr_render.cpp, Raster::draw) na CPU, sem GPU.
#include "test.h"

#include "render/frame_prep.h"

#include <cmath>
#include <limits>

namespace nr = bs::native::render;

namespace {
nr::Vertex vtx(float x, float y, float z, uint32_t c = 0xFFFFFFFF) {
    nr::Vertex v;
    v.x = x, v.y = y, v.inv_w = z, v.color = c;
    return v;
}
void add(nr::CommandList& cl, const nr::DrawState& s, std::initializer_list<nr::Vertex> vs) {
    cl.draws.push_back({s, uint32_t(cl.vertices.size()), uint32_t(vs.size())});
    cl.vertices.insert(cl.vertices.end(), vs);
}
} // namespace

// Strip de 4: dois triangulos, o segundo com os dois primeiros trocados (k impar), num lote so
BS_TEST(frame_prep_strip_order_and_batching) {
    nr::CommandList cl;
    add(cl, {}, {vtx(0, 0, 1), vtx(10, 0, 1), vtx(0, 10, 1), vtx(10, 10, 1)});
    nr::PreparedFrame p;
    nr::prepare_frame(cl, p);
    BS_CHECK(p.triangles == 2 && p.batches.size() == 1 && p.batches[0].count == 6);
    BS_CHECK(p.vertices[3].x == 0 && p.vertices[3].y == 10 && p.vertices[4].x == 10 && p.vertices[4].y == 0);
    BS_CHECK(p.vertices[0].seq == 1 && p.vertices[3].seq == 2);
}

// Culling como o software: area nula e nao finita sempre; pequeno (|area| < 2 * FPU_CULL_VAL); 2 descarta
// area < 0 (anti-horario na tela), 3 descarta area > 0
BS_TEST(frame_prep_culling_rules) {
    auto count = [](nr::Cull c, float small, std::initializer_list<nr::Vertex> vs) {
        nr::CommandList cl;
        cl.frame.small_cull = small;
        nr::DrawState s;
        s.cull = c;
        add(cl, s, vs);
        nr::PreparedFrame p;
        nr::prepare_frame(cl, p);
        return p.triangles;
    };
    const auto cw = {vtx(0, 0, 1), vtx(10, 0, 1), vtx(0, 10, 1)};   // area > 0: horario na tela
    const auto ccw = {vtx(0, 0, 1), vtx(0, 10, 1), vtx(10, 0, 1)};  // area < 0
    BS_CHECK(count(nr::Cull::None, 0, cw) == 1 && count(nr::Cull::None, 0, ccw) == 1);
    BS_CHECK(count(nr::Cull::CounterClockwise, 0, cw) == 1 && count(nr::Cull::CounterClockwise, 0, ccw) == 0);
    BS_CHECK(count(nr::Cull::Clockwise, 0, cw) == 0 && count(nr::Cull::Clockwise, 0, ccw) == 1);
    BS_CHECK(count(nr::Cull::Small, 50, cw) == 1 && count(nr::Cull::Small, 51, cw) == 0);  // |area| = 100
    BS_CHECK(count(nr::Cull::None, 0, {vtx(0, 0, 1), vtx(5, 5, 1), vtx(10, 10, 1)}) == 0);  // colinear
    const float nan = std::numeric_limits<float>::quiet_NaN();
    BS_CHECK(count(nr::Cull::None, 0, {vtx(0, 0, 1), vtx(nan, 0, 1), vtx(0, 10, 1)}) == 0);
    BS_CHECK(count(nr::Cull::None, 0, {vtx(0, 0, nan), vtx(10, 0, 1), vtx(0, 10, 1)}) == 0);  // 1/w NaN
}

// Fases na ordem do PVR (opaca, punch-through, translucida), qualquer que seja a submissao; sombreamento
// plano com a cor do ultimo vertice; autosort conforme o quadro
BS_TEST(frame_prep_phases_flat_color_and_autosort) {
    nr::CommandList cl;
    nr::DrawState   tr, pt, op;
    tr.list = nr::ListType::Translucent, pt.list = nr::ListType::PunchThrough;
    op.gouraud = false;
    add(cl, tr, {vtx(0, 0, 1), vtx(10, 0, 1), vtx(0, 10, 1)});
    add(cl, pt, {vtx(0, 0, 1), vtx(10, 0, 1), vtx(0, 10, 1)});
    add(cl, op, {vtx(0, 0, 1, 0xFF0000FF), vtx(10, 0, 1, 0xFF00FF00), vtx(0, 10, 1, 0xFFFF0000)});
    nr::DrawState mod;
    mod.list = nr::ListType::OpaqueModifier;
    add(cl, mod, {vtx(0, 0, 1), vtx(10, 0, 1), vtx(0, 10, 1)});  // volumes: ainda nao desenhados
    nr::PreparedFrame p;
    nr::prepare_frame(cl, p);
    BS_CHECK(p.batches.size() == 3);
    BS_CHECK(p.batches[0].mode == nr::PassMode::Opaque && p.batches[1].mode == nr::PassMode::PunchThrough);
    BS_CHECK(p.batches[2].mode == nr::PassMode::TranslucentAutosort && p.has_autosort);
    for (int i = 0; i < 3; ++i) BS_CHECK(p.vertices[i].color == 0xFFFF0000);
    cl.frame.translucent_autosort = false;
    nr::prepare_frame(cl, p);
    BS_CHECK(p.batches[2].mode == nr::PassMode::TranslucentOrdered && !p.has_autosort);
}

// Modo fiel: cada triangulo vira o retangulo envolvente do software em pixels inteiros (floor(min) a ceil(max),
// limitado ao quadro e ao recorte), 6 vertices, com o indice do triangulo para o shader
BS_TEST(frame_prep_exact_bounding_rectangles) {
    nr::CommandList cl;
    cl.frame.width = 32, cl.frame.height = 32;
    add(cl, {}, {vtx(1.5f, 2.2f, 1), vtx(40, 3, 1), vtx(2, 9.1f, 1)});
    nr::PreparedFrame p;
    nr::prepare_frame(cl, p, true);
    BS_CHECK(p.triangles == 1 && p.batches.size() == 1 && p.batches[0].count == 6 && p.vertices.size() == 6);
    BS_CHECK(p.vertices[0].x == 1 && p.vertices[0].y == 2 && p.vertices[4].x == 32 && p.vertices[4].y == 11);
    BS_CHECK(p.vertices[5].tri == 0 && p.tri_data.size() == nr::kTriWords);
    nr::DrawState in;
    in.clip = nr::Clip::Inside, in.clip_x0 = 0, in.clip_x1 = 7, in.clip_y0 = 0, in.clip_y1 = 31;
    cl.draws[0].state = in;
    nr::prepare_frame(cl, p, true);
    BS_CHECK(p.vertices[4].x == 8);
}

// A escala da profundidade e uma potencia de 2 (exata) que leva o maior 1/w (e o fundo) para <= 1; recorte
// "dentro" vazio descarta; amostra no canto (HALF_OFFSET bit 0 = 0) desloca meio pixel
BS_TEST(frame_prep_depth_scale_clip_and_sample_offset) {
    nr::CommandList cl;
    cl.frame.clear_depth = 0.0001f;
    add(cl, {}, {vtx(0, 0, 3.0f), vtx(10, 0, 0.5f), vtx(0, 10, 1)});
    nr::PreparedFrame p;
    nr::prepare_frame(cl, p);
    BS_CHECK(p.depth_scale == 0.25f && 3.0f * p.depth_scale == 0.75f);
    BS_CHECK(p.depth_bias == 0);  // sem 1/w negativo: so a escala (exata)
    cl.vertices[1].inv_w = -1.5f;  // 1/w negativo: deslocamento pelo menor, faixa [-1.5, 3] -> escala 1/8
    nr::prepare_frame(cl, p);
    BS_CHECK(p.depth_bias == -1.5f && p.depth_scale == 0.125f && (3.0f - p.depth_bias) * p.depth_scale <= 1.f);
    cl.vertices[1].inv_w = 0.5f;
    cl.frame.sample_center = false;
    nr::prepare_frame(cl, p);
    BS_CHECK(p.vertices[1].x == 10.5f && p.vertices[1].y == 0.5f);
    nr::DrawState in;
    in.clip = nr::Clip::Inside, in.clip_x0 = 32, in.clip_x1 = 31;
    cl.draws[0].state = in;
    nr::prepare_frame(cl, p);
    BS_CHECK(p.triangles == 0 && p.culled == 1);
}
