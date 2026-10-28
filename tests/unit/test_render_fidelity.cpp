// N7 (preparacao): as cenas de fidelidade (native/render/fidelity_scenes) sao coerentes, os valores esperados
// saem da GEOMETRIA (avaliador independente abaixo, nao as constantes da cena) e a cena de autosort
// discrimina a ordenacao por triangulo. Sem GPU: o backend Vulkan as executa na integracao.
#include "test.h"

#include "render/fidelity_scenes.h"
#include "render/render_ir.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace nr = bs::native::render;

namespace {
bool close(uint32_t a, uint32_t b, int tol) {
    for (int sh = 0; sh <= 16; sh += 8)
        if (std::abs(int((a >> sh) & 0xFF) - int((b >> sh) & 0xFF)) > tol) return false;
    return true;
}
const nr::FidelityScene* find(const std::vector<nr::FidelityScene>& v, const char* n) {
    for (const auto& s : v)
        if (s.name == n) return &s;
    return nullptr;
}

// ---- avaliador de um pixel, independente da cena: cobertura, 1/w e cor interpolados no centro do pixel e
// as regras do PVR2 da referencia em software (runtime/pvr/pvr_render.cpp): opacos na ordem com o compare do
// desenho; punch-through da frente para tras com alfa >= ref e >=; translucidas com >= forcado contra os
// opacos, ordenadas POR PIXEL (do mais longe ao mais perto, empate na ordem de submissao) ou, no modo nao
// fiel, POR TRIANGULO (media de 1/w dos 3 vertices). Fatores de mistura nao usados pelas cenas: falha.
struct Frag {
    size_t   draw, tri;
    float    inv_w;
    uint32_t argb;
    float    tri_key;
};
// N8.7: fog por tabela (o mesmo modelo de soft_raster.cpp, em double): z = densidade x 1/w em [1, 256)
double fog_alpha(const nr::FrameParams& f, double inv_w) {
    const int    ex = int(int8_t(f.fog_density & 0xFF));
    const double z  = std::min(std::max(double((f.fog_density >> 8) & 0xFF) / 128.0 * std::ldexp(1.0, ex) * inv_w, 1.0), 255.9999);
    const int    e  = int(std::floor(std::log2(z)));
    const double m  = z * 16.0 / std::ldexp(1.0, e) - 16.0;
    const int    i  = std::min(127, int(std::floor(m)) + e * 16);
    const double fr = m - std::floor(m);
    const double hi = (f.fog_table[size_t(i)] >> 8) / 255.0, lo = (f.fog_table[size_t(i)] & 0xFF) / 255.0;
    return hi + (lo - hi) * fr;
}
uint32_t fogged(const nr::FrameParams& f, uint32_t argb, double inv_w) {
    const double a = fog_alpha(f, inv_w);
    uint32_t     o = argb & 0xFF000000u;
    for (int sh = 0; sh <= 16; sh += 8) {
        const double c = ((argb >> sh) & 0xFF) / 255.0, g = ((f.fog_color >> sh) & 0xFF) / 255.0;
        o |= uint32_t(std::lround((c + (g - c) * a) * 255)) << sh;
    }
    return o;
}
// N8.7: o estado de um volume no centro do pixel: paridade dos triangulos a frente de `depth` (ligado, a partir de
// or_from: OU); `covered`: algum triangulo cobre o ponto
bool volume_parity(const nr::FidelityScene& s, const nr::ModifierVolume& m, double x, double y, float depth, bool& covered,
                   bool start) {
    bool parity = start;  // o estado que ficou de um volume aberto
    covered     = false;
    for (uint32_t k = 0; k < m.triangle_count; ++k) {
        const auto& a = s.cl.vertices[m.first_vertex + 3 * k];
        const auto& b = s.cl.vertices[m.first_vertex + 3 * k + 1];
        const auto& c = s.cl.vertices[m.first_vertex + 3 * k + 2];
        const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (area == 0) continue;
        const double w0 = ((b.x - x) * (c.y - y) - (b.y - y) * (c.x - x)) / area;
        const double w1 = ((c.x - x) * (a.y - y) - (c.y - y) * (a.x - x)) / area;
        const double w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        covered = true;
        if (a.inv_w * w0 + b.inv_w * w1 + c.inv_w * w2 > depth) parity = k >= m.or_from ? true : !parity;
    }
    return parity;
}
uint32_t sat_add(uint32_t a, uint32_t b) {
    uint32_t o = 0;
    for (int sh = 0; sh <= 16; sh += 8) o |= std::min(255u, ((a >> sh) & 0xFF) + ((b >> sh) & 0xFF)) << sh;
    return o;
}
bool depth_pass(nr::DepthTest t, float f, float d) {
    switch (t) {
    case nr::DepthTest::Never: return false;
    case nr::DepthTest::Less: return f < d;
    case nr::DepthTest::Equal: return f == d;
    case nr::DepthTest::LessEqual: return f <= d;
    case nr::DepthTest::Greater: return f > d;
    case nr::DepthTest::NotEqual: return f != d;
    case nr::DepthTest::GreaterEqual: return f >= d;
    default: return true;
    }
}
std::optional<double> factor(nr::Blend b, uint32_t srcA) {
    switch (b) {
    case nr::Blend::Zero: return 0.0;
    case nr::Blend::One: return 1.0;
    case nr::Blend::SrcAlpha: return srcA / 255.0;
    case nr::Blend::InvSrcAlpha: return 1.0 - srcA / 255.0;
    default: return std::nullopt;
    }
}
std::vector<Frag> fragments(const nr::FidelityScene& s, uint32_t px, uint32_t py) {
    std::vector<Frag> out;
    const double      x = px + 0.5, y = py + 0.5;
    for (size_t di = 0; di < s.cl.draws.size(); ++di) {
        const auto& d = s.cl.draws[di];
        for (uint32_t t = 0; t + 2 < d.vertex_count; ++t) {
            const auto& a = s.cl.vertices[d.first_vertex + t];
            const auto& b = s.cl.vertices[d.first_vertex + t + 1];
            const auto& c = s.cl.vertices[d.first_vertex + t + 2];
            const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (area == 0) continue;
            const double w0 = ((b.x - x) * (c.y - y) - (b.y - y) * (c.x - x)) / area;
            const double w1 = ((c.x - x) * (a.y - y) - (c.y - y) * (a.x - x)) / area;
            const double w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            auto lerp = [&](uint32_t ca, uint32_t cb, uint32_t cc, int sh) {
                return uint32_t(std::lround(((ca >> sh) & 0xFF) * w0 + ((cb >> sh) & 0xFF) * w1 + ((cc >> sh) & 0xFF) * w2));
            };
            uint32_t base = 0, off = 0;
            for (int sh = 0; sh <= 24; sh += 8) base |= lerp(a.color, b.color, c.color, sh) << sh;
            for (int sh = 0; sh <= 16; sh += 8) off |= lerp(a.offset, b.offset, c.offset, sh) << sh;
            uint32_t argb = base;
            if (s.draw_texture[di] >= 0) {  // textura (pontual) modulando a base
                const auto&    tx = s.textures[size_t(s.draw_texture[di])];
                const double   u = a.u * w0 + b.u * w1 + c.u * w2, v = a.v * w0 + b.v * w1 + c.v * w2;
                const uint32_t tu = std::min(tx.desc.width - 1, uint32_t(u * tx.desc.width));
                const uint32_t tv = std::min(tx.desc.height - 1, uint32_t(v * tx.desc.height));
                const uint32_t tex = tx.argb[size_t(tv) * tx.desc.width + tu];
                argb = base & 0xFF000000u;
                for (int sh = 0; sh <= 16; sh += 8) argb |= ((((tex >> sh) & 0xFF) * ((base >> sh) & 0xFF) + 127) / 255) << sh;
            }
            if (d.state.offset_color) argb = (argb & 0xFF000000u) | sat_add(argb & 0xFFFFFF, off);
            const float iw = float(a.inv_w * w0 + b.inv_w * w1 + c.inv_w * w2);
            if (d.state.fog == nr::Fog::Table) argb = fogged(s.cl.frame, argb, iw);
            out.push_back({di, t, iw, argb, (a.inv_w + b.inv_w + c.inv_w) / 3.0f});
        }
    }
    return out;
}
// devolve -1 se a cena usar algo que o avaliador nao conhece
int64_t evaluate(const nr::FidelityScene& s, uint32_t px, uint32_t py, bool triangleSort) {
    if (s.image) return s.image_argb[size_t(py) * s.cl.frame.width + px] & 0xFFFFFF;
    const auto fr    = fragments(s, px, py);
    uint32_t   color = s.cl.frame.clear_color & 0xFFFFFF;
    float      depth = 0;  // fundo: o mais longe
    bool       shadow = false;
    for (const auto& f : fr) {
        const auto& st = s.cl.draws[f.draw].state;
        if (st.list != nr::ListType::Opaque) continue;
        if (!depth_pass(st.depth, f.inv_w, depth)) continue;
        color = f.argb & 0xFFFFFF, shadow = st.shadow;
        if (st.depth_write) depth = f.inv_w;
    }
    // N8.7: volumes da lista 1 (fechados, inclusao): dentro se a paridade a frente da profundidade e impar
    if ((s.cl.frame.shadow_scale & 0x100) && shadow) {
        // resultado acumulado na ordem dos volumes: inclusao (dentro) liga; exclusao: so continua o que nao esta dentro
        bool inside = false, pending = false;  // pending: o estado de um volume aberto que segue para o proximo
        for (const auto& m : s.cl.modifiers) {
            if (m.list != nr::ListType::OpaqueModifier) continue;
            bool       covered = false;
            const bool p       = volume_parity(s, m, px + 0.5, py + 0.5, depth, covered, pending);
            if (!m.closed || !covered) {  // a soma so toca os pixels cobertos: o estado continua
                pending = p;
                continue;
            }
            pending = false;
            inside  = m.inclusion ? (inside || p) : (inside && !p);
        }
        if (inside) {
            const uint32_t k = s.cl.frame.shadow_scale & 0xFF;
            uint32_t       o = 0;
            for (int sh = 0; sh <= 16; sh += 8) o |= ((((color >> sh) & 0xFF) * k) >> 8) << sh;
            color = o;
        }
    }
    const uint32_t ref = uint32_t(std::lround(s.cl.frame.punch_through_ref * 255));
    const Frag*    pt  = nullptr;
    for (const auto& f : fr) {  // frente para tras: o mais perto que passa no limiar; empate, o primeiro
        if (s.cl.draws[f.draw].state.list != nr::ListType::PunchThrough) continue;
        if ((f.argb >> 24) < ref || f.inv_w < depth) continue;
        if (!pt || f.inv_w > pt->inv_w) pt = &f;
    }
    if (pt) color = pt->argb & 0xFFFFFF, depth = pt->inv_w;
    std::vector<Frag> tr;
    for (const auto& f : fr)
        if (s.cl.draws[f.draw].state.list == nr::ListType::Translucent && f.inv_w >= depth) tr.push_back(f);
    std::stable_sort(tr.begin(), tr.end(), [&](const Frag& a, const Frag& b) {
        return triangleSort ? a.tri_key < b.tri_key : a.inv_w < b.inv_w;
    });
    for (const auto& f : tr) {
        const auto&    st = s.cl.draws[f.draw].state;
        const uint32_t sa = f.argb >> 24;
        auto           fs = factor(st.src, sa), fd = factor(st.dst, sa);
        if (!fs || !fd) return -1;
        uint32_t o = 0;
        for (int sh = 0; sh <= 16; sh += 8) {
            const double c = ((f.argb >> sh) & 0xFF) * *fs + ((color >> sh) & 0xFF) * *fd;
            o |= uint32_t(std::min(255L, std::lround(c))) << sh;
        }
        color = o;
    }
    return color;
}
} // namespace

// Toda cena: nome unico, regra citada, IR valida, provas dentro do quadro, textura por desenho coerente;
// as fieis nao bloqueadas tem provas; as bloqueadas tem motivo e nenhuma prova (nunca passam em silencio)
BS_TEST(render_fidelity_scenes_coherent) {
    const auto            v = nr::fidelity_scenes();
    std::set<std::string> names;
    int                   faithful = 0, enhancement = 0, blocked = 0;
    for (const auto& s : v) {
        BS_CHECK(names.insert(s.name).second && !s.rule.empty());
        std::string err;
        if (!s.image) BS_CHECK(nr::validate(s.cl, &err));
        if (s.image) BS_CHECK(s.image_argb.size() == size_t(s.cl.frame.width) * s.cl.frame.height && s.cl.draws.empty());
        BS_CHECK(s.draw_texture.size() == s.cl.draws.size());
        for (int32_t t : s.draw_texture) BS_CHECK(t < int32_t(s.textures.size()));
        for (const auto& t : s.textures) BS_CHECK(t.argb.size() == size_t(t.desc.width) * t.desc.height);
        for (const auto& p : s.probes) BS_CHECK(p.x < s.cl.frame.width && p.y < s.cl.frame.height && p.rgb <= 0xFFFFFF);
        if (!s.blocked.empty()) {
            ++blocked;
            BS_CHECK(s.probes.empty());
        } else {
            BS_CHECK(!s.probes.empty());
        }
        (s.mode == nr::SceneMode::Faithful ? faithful : enhancement)++;
    }
    BS_CHECK(faithful >= 11 && enhancement >= 1 && blocked == 1);
    BS_CHECK(find(v, "modificador_translucido") && !find(v, "modificador_translucido")->blocked.empty());
    BS_CHECK(find(v, "modificador_opaco") && find(v, "modificador_opaco")->blocked.empty());  // N8.7: fiel
    BS_CHECK(find(v, "fog_tabela") && find(v, "fog_tabela")->blocked.empty());
}

// Os valores esperados de todas as provas saem da geometria pelo avaliador independente (revisao Codex
// 2026-10-20, MEDIA): fieis por pixel, a melhoria por triangulo
BS_TEST(render_fidelity_probes_match_independent_evaluator) {
    for (const auto& s : nr::fidelity_scenes()) {
        if (!s.blocked.empty()) continue;
        const bool tri = s.mode == nr::SceneMode::Enhancement;
        for (const auto& p : s.probes) {
            const int64_t got = evaluate(s, p.x, p.y, tri);
            const bool    ok  = got >= 0 && close(uint32_t(got), p.rgb, p.tolerance);
            if (!ok) std::fprintf(stderr, "  cena %s (%u,%u): avaliador %06llX, esperado %06X\n", s.name.c_str(), p.x, p.y,
                                  static_cast<unsigned long long>(got), p.rgb);
            BS_CHECK(ok);
            if (p.rgb_if_triangle_sorted >= 0) {
                const int64_t ts = evaluate(s, p.x, p.y, true);
                BS_CHECK(ts >= 0 && close(uint32_t(ts), uint32_t(p.rgb_if_triangle_sorted), p.tolerance));
            }
        }
    }
}

// O autosort por pixel e obrigatorio no modo fiel: na cena das translucidas cruzadas, o avaliador por
// triangulo erra pelo menos uma prova (fora da tolerancia); a ordenacao por triangulo so existe como MELHORIA
BS_TEST(render_fidelity_autosort_discriminates_triangle_sort) {
    const auto  v = nr::fidelity_scenes();
    const auto* s = find(v, "autosort_cruzadas");
    BS_CHECK(s && s->mode == nr::SceneMode::Faithful);
    int discriminating = 0;
    for (const auto& p : s->probes) {
        const int64_t px = evaluate(*s, p.x, p.y, false), tr = evaluate(*s, p.x, p.y, true);
        if (px >= 0 && tr >= 0 && !close(uint32_t(px), uint32_t(tr), p.tolerance * 4)) ++discriminating;
    }
    BS_CHECK(discriminating >= 1);
    const auto* e = find(v, "translucidas_por_triangulo");
    BS_CHECK(e && e->mode == nr::SceneMode::Enhancement && e->probes.size() == s->probes.size());
}

// Regras ja provadas na referencia em software (tests/unit/test_render_formats.cpp), conferidas pelo avaliador
BS_TEST(render_fidelity_expected_values) {
    const auto v = nr::fidelity_scenes();
    BS_CHECK(nr::blend_rgb(0xFF0000, 0x0000FF, 0x80) == 0x80007F);
    BS_CHECK(evaluate(*find(v, "autosort_forca_maior_igual"), 32, 32, false) == 0xFF0000);  // a de tras some
    const auto* pt = find(v, "punch_through_limiar_e_ordem");
    BS_CHECK(evaluate(*pt, 8, 32, false) == 0xFF0000 && evaluate(*pt, 24, 32, false) == 0x0000FF);
    BS_CHECK(evaluate(*pt, 40, 32, false) == 0xFF0000 && evaluate(*pt, 56, 32, false) == 0x00FF00);
    BS_CHECK(evaluate(*find(v, "mistura_one_one"), 56, 32, false) == 0xFFFFFF);  // satura
    const auto* vid = find(v, "video_quadro_pronto");
    BS_CHECK(vid->image && vid->probes[1].rgb == (vid->image_argb[1000] & 0xFFFFFF) && vid->probes[1].tolerance == 0);
}
