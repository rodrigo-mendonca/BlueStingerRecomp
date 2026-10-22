#include "render/fidelity_scenes.h"

#include <cmath>

namespace bs::native::render {

namespace {
constexpr uint32_t kSize = 64;  // quadro pequeno: as regras nao dependem da resolucao

uint32_t rgb(uint32_t argb) { return argb & 0x00FFFFFFu; }

// Faixa de 4 vertices cobrindo [x0,x1) x [0,64): 1/w por coluna (esquerda, direita), cor e alfa iguais
void quad(FidelityScene& s, DrawState st, float x0, float x1, float wl, float wr, uint32_t color, uint32_t offset = 0,
          int32_t tex = -1) {
    Draw d;
    d.state        = st;
    d.first_vertex = uint32_t(s.cl.vertices.size());
    d.vertex_count = 4;
    const float u1 = 1.0f;
    s.cl.vertices.push_back({x0, 0, wl, 0, 0, color, offset});
    s.cl.vertices.push_back({x1, 0, wr, u1, 0, color, offset});
    s.cl.vertices.push_back({x0, float(kSize), wl, 0, 1, color, offset});
    s.cl.vertices.push_back({x1, float(kSize), wr, u1, 1, color, offset});
    s.cl.draws.push_back(d);
    s.draw_texture.push_back(tex);
}

// Um unico triangulo (0,0) (64,0) (0,64): 1/w = wl na coluna 0 e wr na coluna 64 (linear em x). Uma camada
// inteira num triangulo so: uma ordenacao por triangulo tem UMA ordem para todos os pixels dele.
void tri(FidelityScene& s, DrawState st, float wl, float wr, uint32_t color) {
    Draw d;
    d.state        = st;
    d.first_vertex = uint32_t(s.cl.vertices.size());
    d.vertex_count = 3;
    s.cl.vertices.push_back({0, 0, wl, 0, 0, color, 0});
    s.cl.vertices.push_back({float(kSize), 0, wr, 1, 0, color, 0});
    s.cl.vertices.push_back({0, float(kSize), wl, 0, 1, color, 0});
    s.cl.draws.push_back(d);
    s.draw_texture.push_back(-1);
}

FidelityScene base(const char* name, const char* rule, uint32_t clear) {
    FidelityScene s;
    s.name               = name;
    s.rule               = rule;
    s.cl.frame.width     = kSize;
    s.cl.frame.height    = kSize;
    s.cl.frame.clear_color = clear;
    return s;
}

DrawState opaque() {
    DrawState st;
    st.list   = ListType::Opaque;
    st.filter = Filter::Point;
    return st;
}
DrawState translucent(Blend src = Blend::SrcAlpha, Blend dst = Blend::InvSrcAlpha) {
    DrawState st = opaque();
    st.list        = ListType::Translucent;
    st.src         = src;
    st.dst         = dst;
    st.depth_write = false;
    st.use_alpha   = true;
    return st;
}
DrawState punch() {
    DrawState st = opaque();
    st.list      = ListType::PunchThrough;
    st.use_alpha = true;
    st.depth     = DepthTest::Always;  // o compare do poligono e ignorado (>=, frente para tras)
    return st;
}
} // namespace

uint32_t blend_rgb(uint32_t s, uint32_t d, uint32_t alpha) {
    const double a = alpha / 255.0;
    uint32_t     o = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const double c = ((s >> sh) & 0xFF) * a + ((d >> sh) & 0xFF) * (1 - a);
        o |= uint32_t(std::lround(c)) << sh;
    }
    return o;
}

std::vector<FidelityScene> fidelity_scenes() {
    std::vector<FidelityScene> v;
    const uint32_t blue = 0xFF0000FF, red = 0xFFFF0000, green = 0xFF00FF00;

    {  // translucidas que se cruzam: so a ordem POR PIXEL acerta os dois lados
        auto s = base("autosort_cruzadas", "autosort por pixel (ISP_FEED_CFG bit 0 = 0): fragmentos do mais longe ao mais perto "
                                           "por 1/w; pvr_render.cpp, render_translucent_autosort_vs_presort",
                      blue);
        // cada camada e UM triangulo (revisao Codex 2026-10-20, ALTA: um quad sao dois triangulos cujas medias de
        // profundidade se invertem entre as metades, e a ordenacao por triangulo acertava os dois lados)
        tri(s, translucent(), 1, 3, 0x80000000 | rgb(red));    // T1: 1/w 1 -> 3 (perto a direita); media 5/3
        tri(s, translucent(), 3, 1, 0x80000000 | rgb(green));  // T2: 3 -> 1 (perto a esquerda); media 7/3
        const uint32_t t1first = blend_rgb(rgb(green), blend_rgb(rgb(red), rgb(blue), 0x80), 0x80);
        const uint32_t t2first = blend_rgb(rgb(red), blend_rgb(rgb(green), rgb(blue), 0x80), 0x80);
        // por triangulo (qualquer chave unica por triangulo: media, minimo, maximo ou submissao): T1 antes de T2
        // em todos os pixels; por pixel: a esquerda T1 esta longe (T1 primeiro), a direita T2 esta longe
        s.probes.push_back({8, 8, t1first, 2, int64_t(t1first)});
        s.probes.push_back({48, 8, t2first, 2, int64_t(t1first)});
        v.push_back(s);
        auto e  = s;  // a mesma cena como MELHORIA nao fiel: ordenacao por triangulo
        e.name  = "translucidas_por_triangulo";
        e.rule  = "opcao NAO fiel (ordenacao por triangulo); nunca usada na comparacao com a referencia";
        e.mode  = SceneMode::Enhancement;
        for (auto& p : e.probes) p.rgb = uint32_t(p.rgb_if_triangle_sorted), p.rgb_if_triangle_sorted = -1;
        v.push_back(e);
    }
    {  // profundidade 1/w com >=: o mais perto vence; empate: o submetido depois vence
        auto s = base("profundidade_maior_igual", "compare >= em 1/w (maior = mais perto); render_depth_compare_modes", blue);
        auto st = opaque();
        quad(s, st, 0, 64, 2, 2, red);
        quad(s, st, 0, 64, 1, 1, green);  // atras: rejeitado
        quad(s, st, 0, 32, 2, 2, blue);   // mesmo 1/w, depois: passa (>=)
        s.probes.push_back({8, 32, rgb(blue)});
        s.probes.push_back({56, 32, rgb(red)});
        v.push_back(s);
    }
    {  // autosort ignora o compare do poligono e usa >= contra os opacos
        auto s = base("autosort_forca_maior_igual", "translucida atras de opaco e rejeitada mesmo com compare ALWAYS; "
                                                    "render_autosort_forces_gequal",
                      0xFF000000);
        quad(s, opaque(), 0, 64, 2, 2, red);
        auto st  = translucent();
        st.depth = DepthTest::Always;
        quad(s, st, 0, 64, 1, 1, 0x80000000 | rgb(green));
        s.probes.push_back({32, 32, rgb(red)});
        v.push_back(s);
    }
    {  // punch-through: alfa >= ref passa; da frente para tras
        auto s = base("punch_through_limiar_e_ordem", "alfa >= PT_ALPHA_REF; frente para tras com >=, compare ignorado; "
                                                      "render_punch_through_alpha_ref, render_punch_through_front_to_back",
                      blue);
        s.cl.frame.punch_through_ref = 128 / 255.0f;
        quad(s, punch(), 0, 16, 2, 2, 0x80000000 | rgb(red));   // alfa 0x80: passa
        quad(s, punch(), 16, 32, 2, 2, 0x7F000000 | rgb(red));  // 0x7F: descartado -> fundo
        quad(s, punch(), 32, 48, 2, 2, 0xFF000000 | rgb(red));  // perto, opaco: vence o verde de tras
        quad(s, punch(), 48, 64, 2, 2, 0x40000000 | rgb(red));  // perto, abaixo do limiar: revela o verde
        quad(s, punch(), 32, 64, 1, 1, 0xFF000000 | rgb(green));  // longe, submetido depois
        s.probes.push_back({8, 32, rgb(red)});
        s.probes.push_back({24, 32, rgb(blue)});
        s.probes.push_back({40, 32, rgb(red)});
        s.probes.push_back({56, 32, rgb(green)});
        v.push_back(s);
    }
    {  // mistura aditiva One/One
        auto s = base("mistura_one_one", "fatores One/One somam e saturam; render_blend_factors", 0xFF202020);
        quad(s, translucent(Blend::One, Blend::One), 0, 32, 2, 2, 0x80404040);
        quad(s, translucent(Blend::One, Blend::One), 32, 64, 2, 2, 0x80F0F0F0);
        s.probes.push_back({8, 32, 0x606060});
        s.probes.push_back({56, 32, 0xFFFFFF});
        v.push_back(s);
    }
    {  // cor base modulando a textura + offset (especular)
        auto s = base("cor_base_e_offset", "textura x cor base + offset; offset negativo ja saturado em 0 no TA; "
                                           "render_texture_shading_instructions_and_offset, render_negative_float_offset_saturates_to_zero",
                      0xFF000000);
        SceneTexture t;
        t.desc = {8, 8, PixelFormat::ARGB8888, 1};
        t.argb.assign(64, 0xFFFFFFFF);
        s.textures.push_back(t);
        auto st         = opaque();
        st.env          = TexEnv::Modulate;
        st.offset_color = true;
        quad(s, st, 0, 64, 2, 2, 0xFF402000, 0xFF003010, 0);
        s.probes.push_back({32, 32, 0x405010});
        v.push_back(s);
    }
    {  // HUD translucido sobre o 3D
        auto s = base("hud_translucido", "sprite translucido do HUD (lista translucida, mais perto) sobre o opaco", 0xFF000000);
        quad(s, opaque(), 0, 64, 1, 1, red);
        quad(s, translucent(), 0, 32, 10, 10, 0x40FFFFFF);
        s.probes.push_back({8, 32, blend_rgb(0xFFFFFF, rgb(red), 0x40)});
        s.probes.push_back({56, 32, rgb(red)});
        v.push_back(s);
    }
    {  // quadro de video (N5) pronto: chega a tela sem alteracao
        auto s  = base("video_quadro_pronto", "present_image: o quadro ARGB do filme nativo (N5) sem mudanca", 0xFF000000);
        s.image = true;
        s.image_argb.resize(kSize * kSize);
        for (uint32_t i = 0; i < kSize * kSize; ++i) s.image_argb[i] = 0xFF000000u | (i * 2654435761u >> 8);
        for (uint32_t p : {0u, 1000u, 4095u}) s.probes.push_back({p % kSize, p / kSize, rgb(s.image_argb[p]), 0});
        v.push_back(s);
    }
    for (auto [name, list] : {std::pair{"modificador_opaco", ListType::OpaqueModifier},
                              std::pair{"modificador_translucido", ListType::TranslucentModifier}}) {
        auto s = base(name, "volume modificador (lista 1/3 do TA)", 0xFF000000);
        quad(s, opaque(), 0, 64, 1, 1, red);
        s.cl.modifiers.push_back({0, 1, true, list});  // 1 triangulo: os 3 primeiros vertices
        s.blocked = "a referencia em software ainda nao aplica volumes (pvr_render.cpp: listas 1 e 3 descartadas) e a IR nao "
                    "tem o segundo conjunto de parametros do poligono (dois volumes); medir o uso no oraculo antes";
        v.push_back(s);
    }
    return v;
}

} // namespace bs::native::render
