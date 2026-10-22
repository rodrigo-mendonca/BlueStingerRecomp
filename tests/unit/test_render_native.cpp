// N7 (preparacao): regras portaveis da renderizacao nativa (native/render), sem GPU.
#include "test.h"

#include "render/render_ir.h"
#include "render/texture_convert.h"

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace nr = bs::native::render;

// texels de 16 bits: extremos e a expansao repetindo os bits altos (a regra do hardware)
BS_TEST(render_texel16_formats) {
    BS_CHECK(nr::texel16_to_argb(0xFFFF, nr::Texel16::Argb1555) == 0xFFFFFFFFu);
    BS_CHECK(nr::texel16_to_argb(0x7FFF, nr::Texel16::Argb1555) == 0x00FFFFFFu);  // alfa de 1 bit
    BS_CHECK(nr::texel16_to_argb(0x8000, nr::Texel16::Argb1555) == 0xFF000000u);
    BS_CHECK(nr::texel16_to_argb(0xF800, nr::Texel16::Rgb565) == 0xFFFF0000u);
    BS_CHECK(nr::texel16_to_argb(0x07E0, nr::Texel16::Rgb565) == 0xFF00FF00u);
    BS_CHECK(nr::texel16_to_argb(0x001F, nr::Texel16::Rgb565) == 0xFF0000FFu);
    BS_CHECK(nr::texel16_to_argb(0x8421, nr::Texel16::Argb4444) == 0x88442211u);  // 4 bits: v*17
    BS_CHECK(nr::texel16_to_argb(0x0421, nr::Texel16::Argb1555) == 0x00080808u);  // 5 bits: 1 -> 8 (1<<3 | 1>>2)
}

// twiddle: Morton com y no bit par; textura retangular em blocos quadrados
BS_TEST(render_twiddle_addressing) {
    // 8x8: (x,y) -> indice
    BS_CHECK(nr::twiddle_index(0, 0, 8, 8) == 0 && nr::twiddle_index(0, 1, 8, 8) == 1 && nr::twiddle_index(1, 0, 8, 8) == 2);
    BS_CHECK(nr::twiddle_index(1, 1, 8, 8) == 3 && nr::twiddle_index(2, 0, 8, 8) == 8 && nr::twiddle_index(7, 7, 8, 8) == 63);
    // bijecao em 32x8 (larga) e 8x32 (alta)
    for (auto [w, h] : {std::pair{32u, 8u}, std::pair{8u, 32u}, std::pair{64u, 64u}}) {
        std::set<uint32_t> seen;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) seen.insert(nr::twiddle_index(x, y, w, h));
        BS_CHECK(seen.size() == size_t(w) * h && *seen.rbegin() == w * h - 1);
    }
    BS_CHECK(nr::twiddle_index(8, 0, 32, 8) == 64);  // segundo bloco 8x8 da textura larga
}

// conversao inteira: twiddled e linear dao a mesma imagem; recusa tamanhos que o PVR nao tem
BS_TEST(render_convert16_textures) {
    const uint32_t        w = 16, h = 8;
    std::vector<uint16_t> lin(w * h), tw(w * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint16_t t = uint16_t(0x8000 | (x << 10) | (y << 5) | ((x + y) & 31));
            lin[y * w + x]                     = t;
            tw[nr::twiddle_index(x, y, w, h)] = t;
        }
    std::vector<uint32_t> a, b;
    BS_CHECK(nr::convert16(lin, w, h, nr::Texel16::Argb1555, false, a) && nr::convert16(tw, w, h, nr::Texel16::Argb1555, true, b));
    BS_CHECK(a == b && a.size() == w * h);
    std::vector<uint32_t> c;
    BS_CHECK(!nr::convert16(lin, 12, 8, nr::Texel16::Rgb565, true, c));   // twiddled nao potencia de 2
    BS_CHECK(!nr::convert16(lin, 4, 4, nr::Texel16::Rgb565, false, c));   // menor que 8
    BS_CHECK(!nr::convert16(lin, 16, 16, nr::Texel16::Rgb565, false, c)); // dados curtos
}

// IR: validacao com o desenho culpado; ordem do PVR (opaca, punch-through, translucida), estavel
BS_TEST(render_ir_validate_and_order) {
    nr::CommandList cl;
    cl.vertices.resize(9);
    auto draw = [&](nr::ListType t, uint32_t first) {
        nr::Draw d;
        d.state.list = t, d.first_vertex = first, d.vertex_count = 3;
        cl.draws.push_back(d);
    };
    draw(nr::ListType::Translucent, 0), draw(nr::ListType::Opaque, 3), draw(nr::ListType::PunchThrough, 6);
    draw(nr::ListType::Opaque, 0), draw(nr::ListType::TranslucentModifier, 3), draw(nr::ListType::OpaqueModifier, 6);
    std::string err;
    BS_CHECK(nr::validate(cl, &err));
    BS_CHECK((nr::draw_order(cl) == std::vector<uint32_t>{1, 3, 5, 2, 0, 4}));
    auto bad = cl;
    bad.draws[2].first_vertex = 8;
    BS_CHECK(!nr::validate(bad, &err) && err.find("desenho 2") != std::string::npos);
    bad = cl;
    bad.draws[0].vertex_count = 2;
    BS_CHECK(!nr::validate(bad, &err) && err.find("menos de 3") != std::string::npos);
    // valores de vertice sao dados do jogo (o jogo envia NaN e 1/w negativo, N7a): nao invalidam a IR
    auto data = cl;
    data.vertices[4].inv_w = -1.0f;
    BS_CHECK(nr::validate(data, &err));
    data.vertices[4].x = std::nanf("");
    BS_CHECK(nr::validate(data, &err));
    bad = cl;
    bad.modifiers.push_back({6, 2, true});  // 2 triangulos a partir do 6: passa do fim
    BS_CHECK(!nr::validate(bad, &err) && err.find("volume 0") != std::string::npos);
    bad = cl;
    bad.frame.width = 0;
    BS_CHECK(!nr::validate(bad, &err));
    bad = cl;
    bad.modifiers.push_back({0, 1, true, nr::ListType::Opaque});  // volume tem que ser de lista modificadora
    BS_CHECK(!nr::validate(bad, &err) && err.find("modificador") != std::string::npos);
}

// volumes opacos e translucidos no mesmo quadro: cada fase com os seus, na ordem de submissao
BS_TEST(render_ir_modifier_phases) {
    nr::CommandList cl;
    cl.vertices.resize(9);
    cl.modifiers.push_back({0, 1, true, nr::ListType::TranslucentModifier});
    cl.modifiers.push_back({3, 1, false, nr::ListType::OpaqueModifier});
    cl.modifiers.push_back({6, 1, true, nr::ListType::TranslucentModifier});
    cl.modifiers.push_back({0, 3, true, nr::ListType::OpaqueModifier});
    BS_CHECK(nr::validate(cl));
    BS_CHECK((nr::modifier_order(cl, nr::ListType::OpaqueModifier) == std::vector<uint32_t>{1, 3}));
    BS_CHECK((nr::modifier_order(cl, nr::ListType::TranslucentModifier) == std::vector<uint32_t>{0, 2}));
}
