// N7a / N8.7 5a (docs/N8-7-KAMUI.md), so a validacao: o adaptador do decodificador das listas do legado
// (pvr/pvr_render.cpp, PvrCaptureSink). Monta a IR pelos retornos do decodificador no renderizador sobre a IR
// (render_core) e e onde a IR e conferida contra o caminho do TA (modo conferir) e o ListSink nativo contra a captura.
#include "render_core.h"

#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "render/render_ir.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <utility>

#if !BS_VALIDACAO
#error "render_capture.cpp (o adaptador do decodificador do legado) so entra no build de validacao"
#endif

namespace bs::transitional::render_capture {
namespace {

constexpr uint32_t R(uint32_t off) { return 0x005F8000 + off; }

float f32(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

nr::ListType list_type(uint32_t l) {
    switch (l) {
    case 1: return nr::ListType::OpaqueModifier;
    case 2: return nr::ListType::Translucent;
    case 3: return nr::ListType::TranslucentModifier;
    case 4: return nr::ListType::PunchThrough;
    default: return nr::ListType::Opaque;
    }
}

TextureKey key_of(const PvrTri& t, uint32_t stride) {
    return {t.tcw, 8u << ((t.tsp >> 3) & 7), 8u << (t.tsp & 7), stride};
}

bool same_vertex(const PvrVertex& a, const PvrVertex& b) {
    return std::memcmp(&a, &b, sizeof a) == 0;  // exato: os dois lados sao os mesmos floats
}

class Capture final : public PvrCaptureSink {
public:
    explicit Capture(Renderer& r) : r_(r) {}

    void begin(uint32_t w, uint32_t h) override {
        r_.frame                = {};
        r_.frame.cl.frame       = frame_params(w, h);
        stride_                 = (pvr_reg(R(0x0E4)) & 0x1F) * 32;  // TEXT_CONTROL
        r_.frame.texture_stride = stride_;
        reset_strip();
        mod_open_ = false, mod_closing_ = false, mod_cur_closed_ = false;  // os volumes recomecam a cada render
        mod_instr_ = 0, mod_closed_ = 0;
        // N8.7: o que os retornos deste render somam nas estatisticas (o ListSink tem de contar o mesmo)
        at_begin_ = {r_.st.two_volume, r_.st.volume_bad_instruction, r_.st.volume_mixed_cull, r_.st.volume_inclusion,
                     r_.st.volume_exclusion, r_.st.volume_or};
    }

    void vertex(const PvrTri& state, const PvrVertex& v, bool end) override {
        strip_.push_back(v);
        const size_t n = strip_.size();
        if (n >= 3) {
            const nr::DrawState s = state_of(state);
            const bool contiguous = open_ >= 0 && r_.frame.cl.draws[size_t(open_)].first_vertex +
                                                          r_.frame.cl.draws[size_t(open_)].vertex_count ==
                                                      r_.frame.cl.vertices.size();  // os vertices do strip sao os ultimos
            if (!tri_mode_ && contiguous && s == r_.frame.cl.draws[size_t(open_)].state) {
                r_.frame.cl.draws[size_t(open_)].vertex_count++;  // o strip continua com o mesmo estado
                r_.frame.cl.vertices.push_back(render_capture::vertex(v));
            } else if (!tri_mode_ && n == 3) {  // o primeiro triangulo do strip abre o desenho
                open_ = int32_t(r_.frame.cl.draws.size());
                r_.frame.cl.draws.push_back({s, uint32_t(r_.frame.cl.vertices.size()), 3});
                for (size_t i = 0; i < 3; ++i) r_.frame.cl.vertices.push_back(render_capture::vertex(strip_[i]));
            } else {
                // estado mudou no meio do strip (recorte do usuario entre vertices): daqui ate o fim do strip, um
                // desenho por triangulo, na ordem exata do decodificador (n par: os dois primeiros trocados)
                tri_mode_ = true, open_ = -1;
                PvrVertex t[3] = {strip_[n - 3], strip_[n - 2], strip_[n - 1]};
                if (n % 2 == 0) std::swap(t[0], t[1]);
                r_.frame.cl.draws.push_back({s, uint32_t(r_.frame.cl.vertices.size()), 3});
                for (const PvrVertex& x : t) r_.frame.cl.vertices.push_back(render_capture::vertex(x));
            }
        }
        if (end) reset_strip();
    }

    void strip_break() override { reset_strip(); }

    void sprite(const PvrTri& state, const PvrVertex& a, const PvrVertex& b, const PvrVertex& c,
                const PvrVertex& d) override {
        // dois desenhos de 3 vertices, (A, B, C) e (A, C, D), na ordem do decodificador: um strip de 4 so os
        // reproduz com os vertices numa ordem ciclica diferente, o que muda as somas da interpolacao
        const nr::DrawState s = state_of(state);
        const PvrVertex* t1[3] = {&a, &b, &c};
        const PvrVertex* t2[3] = {&a, &c, &d};
        for (const PvrVertex* const* t : {t1, t2}) {
            r_.frame.cl.draws.push_back({s, uint32_t(r_.frame.cl.vertices.size()), 3});
            for (int i = 0; i < 3; ++i) r_.frame.cl.vertices.push_back(render_capture::vertex(*t[i]));
        }
        ++r_.frame.sprites;
        open_ = -1;
    }

    void global(const TaParam& g) override {
        // volume modificador: a instrucao (ISP bits 30-29 do global) 0 = poligono do volume, 1 = ultimo poligono
        // (inclusao), 2 = ultimo poligono (exclusao). Os triangulos depois de um global 1/2 fecham o volume; o
        // proximo global (de qualquer lista) comeca outro (revisao Codex 2026-10-21: globais iguais nao se fundem)
        if (mod_closing_) mod_open_ = false;
        mod_closing_ = false;
        if (g.list != 1 && g.list != 3) {
            if (g.w[0] & 0x40) ++r_.st.two_volume;  // N8.7: sem suporte na IR (o decodificador descarta o poligono)
            return;
        }
        mod_instr_   = (g.w[1] >> 29) & 7;
        mod_closing_ = mod_instr_ == 1 || mod_instr_ == 2;
        mod_cull_    = uint8_t((g.w[1] >> 27) & 3);  // N8.7: descarte de faces do volume
        mod_last_    = g.w[0] & 0x40;                 // PCW bit 6 ("volume"): fechamento por inversao; sem ele, por OU
        if (mod_instr_ > 2) ++r_.st.volume_bad_instruction;
    }

    void modifier(const TaParam&, const TaParam& p) override {
        // vertice de volume modificador (64 bytes): A, B, C em x, y, z nas palavras 1..9
        ++r_.frame.modifier_params;
        if (!mod_open_ || r_.frame.cl.modifiers.back().list != list_type(p.list)) {
            r_.frame.cl.modifiers.push_back({uint32_t(r_.frame.cl.vertices.size()), 0, true, list_type(p.list)});
            r_.frame.cl.modifiers.back().cull = mod_cull_;
            ++r_.frame.modifier_volumes;
            mod_open_ = true, mod_cur_closed_ = false;
        }
        nr::ModifierVolume& mv = r_.frame.cl.modifiers.back();
        if (mv.cull != mod_cull_) ++r_.st.volume_mixed_cull;  // a IR guarda um descarte por volume
        if (mod_closing_) {
            mv.inclusion = mod_instr_ == 1, mv.closed = true;
            if (!mod_last_ && mv.or_from == ~0u) mv.or_from = mv.triangle_count;  // este parametro entra por OU
            if (!mod_cur_closed_) {
                mod_cur_closed_ = true, ++mod_closed_;
                ++(mv.inclusion ? r_.st.volume_inclusion : r_.st.volume_exclusion);
                r_.st.volume_or += mv.or_from != ~0u;
            }
        }
        for (int i = 0; i < 3; ++i) {
            nr::Vertex v;
            v.x = f32(p.w[1 + 3 * i]), v.y = f32(p.w[2 + 3 * i]), v.inv_w = f32(p.w[3 + 3 * i]);
            r_.frame.cl.vertices.push_back(v);
        }
        r_.frame.cl.modifiers.back().triangle_count++;
    }

    void skipped(const TaParam&) override { ++r_.frame.skipped_params; }

    bool render(const PvrFrame&, uint32_t w, uint32_t h, std::vector<uint32_t>& image) override {
        return r_.draw(w, h, image);
    }

    void end(const PvrFrame& used, std::span<const PvrTri> drawn, std::span<const uint32_t> image, uint32_t w,
             uint32_t h) override {
        r_.frame.modifier_unclosed = r_.frame.modifier_volumes - mod_closed_;
        r_.st.modifier_unclosed += r_.frame.modifier_unclosed;
        ++r_.st.renders;
        check_list_sink(drawn);
        if (r_.mode == Mode::Compare && r_.vk_ok()) r_.compare(image, w, h);
        r_.account();
        if (r_.mode != Mode::Check || (r_.st.renders - 1) % r_.every) return;
        check(used, drawn, image, w, h);
    }

private:
    void check(const PvrFrame& used, std::span<const PvrTri> drawn, std::span<const uint32_t> image, uint32_t w, uint32_t h) {
        ++r_.st.checked;
        auto fail = [&](uint64_t& counter, const std::string& what) {
            ++counter;
            if (r_.st.first_error.empty()) r_.st.first_error = "render " + std::to_string(r_.st.renders) + ": " + what;
        };
        std::string err;
        if (!valid(r_.frame, &err)) {
            fail(r_.st.invalid, "IR invalida: " + err);
            return;  // indices nao confiaveis: nada mais e percorrido
        }
        const PvrFrame from_ir = pvr_frame(r_.frame);
        if (!(from_ir == used)) fail(r_.st.frame_mismatch, "FrameParams nao reproduzem os registradores do quadro");
        const std::vector<PvrTri> tris = triangles(r_.frame);
        r_.st.triangles += tris.size();
        if (tris.size() != drawn.size()) {
            fail(r_.st.triangle_mismatch, "IR com " + std::to_string(tris.size()) + " triangulos, rasterizador " +
                                           std::to_string(drawn.size()));
        } else {
            for (size_t i = 0; i < tris.size(); ++i) {
                const PvrTri& a = tris[i];
                const PvrTri& b = drawn[i];
                const bool    vtx = same_vertex(a.v[0], b.v[0]) && same_vertex(a.v[1], b.v[1]) && same_vertex(a.v[2], b.v[2]);
                const bool    est = a.list == b.list && state_of(a) == state_of(b) &&
                                 (!b.tex || key_of(a, stride_) == key_of(b, stride_));
                if (!vtx || !est) {
                    fail(r_.st.triangle_mismatch, "triangulo " + std::to_string(i) + (vtx ? " (estado)" : " (vertices)"));
                    break;
                }
            }
        }
        std::vector<soft::Volume>    vols;
        std::vector<soft::VolumeTri> vtris;
        soft::tri_volumes(r_.frame.cl, vols, vtris);
        const std::vector<uint32_t> again = pvr_render_triangles(tris, vols, vtris, from_ir, w, h);
        if (again.size() != image.size() || std::memcmp(again.data(), image.data(), image.size() * 4) != 0) {
            size_t diff = 0;
            for (size_t i = 0; i < std::min(again.size(), image.size()); ++i) diff += again[i] != image[i];
            fail(r_.st.image_mismatch, "imagem a partir da IR difere em " + std::to_string(diff) + " pixels");
        }
        // N8.7: o dispositivo em software (texturas convertidas, nenhuma VRAM) desenha a mesma imagem do caminho do TA
        std::vector<uint32_t> soft_img;
        if (!r_.soft_render(w, h, soft_img)) {
            fail(r_.st.soft_image_mismatch, "renderizador em software sobre a IR falhou: " + r_.st.soft_error);
        } else if (soft_img.size() != image.size() || std::memcmp(soft_img.data(), image.data(), image.size() * 4) != 0) {
            size_t diff = 0;
            for (size_t i = 0; i < std::min(soft_img.size(), image.size()); ++i) diff += soft_img[i] != image[i];
            fail(r_.st.soft_image_mismatch, "software sobre a IR difere em " + std::to_string(diff) + " pixels");
        }
        for (const TextureKey& k : r_.frame.textures) {
            std::vector<uint32_t> argb;
            if (!convert_texture(k, argb)) {
                ++r_.st.textures_without_native;
                continue;
            }
            ++r_.st.textures_checked;
            const nr::TextureId id = nr::texture_id(k.width, k.height, argb);
            if (keys_.insert({k.tcw, (uint64_t(k.width) << 32) | k.height}).second) ++r_.st.distinct_texture_keys;
            if (ids_.insert({id.content, (uint64_t(id.width) << 32) | id.height}).second) ++r_.st.distinct_texture_ids;
            const TexInfo t{k.tcw, k.width, k.height, k.stride};
            for (uint32_t y = 0, bad = 0; y < k.height && !bad; ++y)
                for (uint32_t x = 0; x < k.width; ++x)
                    if (argb[size_t(y) * k.width + x] != pvr_texel(t, x, y)) {
                        char b[96];
                        std::snprintf(b, sizeof b, "textura TCW %08X %ux%u: texel (%u,%u)", k.tcw, k.width, k.height, x, y);
                        fail(r_.st.texture_mismatch, b);
                        bad = 1;
                        break;
                    }
        }
    }

    std::vector<PvrVertex> strip_;
    int32_t                open_     = -1;
    bool                   tri_mode_ = false;
    uint32_t               stride_   = 0;
    bool                   mod_open_ = false, mod_closing_ = false, mod_cur_closed_ = false;
    uint32_t               mod_instr_ = 0, mod_closed_ = 0;  // instrucao do ultimo global; volumes com ultimo poligono
    uint8_t                mod_cull_ = 0;      // N8.7: descarte do ultimo global de volume
    bool                   mod_last_ = false;  // N8.7: PCW bit 6 do ultimo global de volume
    std::set<std::pair<uint64_t, uint64_t>> keys_, ids_;  // para o resumo: quantas chaves e quantos conteudos
    std::array<uint64_t, 6>                 at_begin_{};  // N8.7: as estatisticas no inicio do render (check_list_sink)
    Renderer&                               r_;

    // N8.7: o ListSink (native/render/kamui) decodifica as mesmas listas direto para a IR; tem de dar exatamente esta
    void check_list_sink(std::span<const PvrTri> drawn) {
        namespace km = bs::native::render::kamui;
        km::ListFrame lf;
        ta_list_sink().build(r_.frame.cl.frame, stride_, lf);
        // negativos (BS_LISTA_NATIVA_MUTACAO=<campo>): o comparador tem de acusar cada um. vertice: o primeiro vertice
        // muda meio pixel; suporte, omitidos, dois_volumes, global_dois_volumes, instrucao, descarte: a contagem + 1
        static const std::string mut = [] {
            const char* v = std::getenv("BS_LISTA_NATIVA_MUTACAO");
            return std::string(v ? v : "");
        }();
        if ((mut == "1" || mut == "vertice") && !lf.cl.vertices.empty()) lf.cl.vertices[0].x += 0.5f;
        if (mut == "suporte") ++lf.unsupported;
        if (mut == "omitidos") ++lf.skipped;
        if (mut == "dois_volumes") ++lf.two_volume;
        if (mut == "global_dois_volumes") ++lf.global_two_volume;
        if (mut == "instrucao") ++lf.volume_bad_instruction;
        if (mut == "descarte") ++lf.volume_mixed_cull;
        ++r_.st.list_checked;
        r_.st.list_volume_inclusion += lf.volume_inclusion, r_.st.list_volume_exclusion += lf.volume_exclusion;
        r_.st.list_volume_or += lf.volume_or;
        std::string why;
        // os parametros e os bytes que o ListSink juntou (pelos blocos) sao os da FIFO do legado
        const auto& lp = ta_list_sink().params();
        const auto& tp = ta_params();
        if (ta_list_sink().bytes() != ta_bytes())
            why = "bytes (" + std::to_string(ta_list_sink().bytes()) + " x " + std::to_string(ta_bytes()) + ")";
        else if (lp.size() != tp.size())
            why = "parametros (" + std::to_string(lp.size()) + " x " + std::to_string(tp.size()) + ")";
        for (size_t i = 0; why.empty() && i < lp.size(); ++i)
            if (lp[i].list != tp[i].list || lp[i].words != tp[i].words || std::memcmp(lp[i].w, tp[i].w, lp[i].words * 4))
                why = "parametro " + std::to_string(i);
        if (!why.empty()) {
            ++r_.st.list_mismatch;
            if (r_.st.list_error.empty()) r_.st.list_error = "render " + std::to_string(r_.st.renders) + ": " + why;
            return;
        }
        const nr::CommandList &a = lf.cl, &b = r_.frame.cl;
        if (a.vertices.size() != b.vertices.size() ||
            (!a.vertices.empty() && std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(nr::Vertex))))
            why = "vertices (" + std::to_string(a.vertices.size()) + " x " + std::to_string(b.vertices.size()) + ")";
        else if (a.draws.size() != b.draws.size())
            why = "desenhos (" + std::to_string(a.draws.size()) + " x " + std::to_string(b.draws.size()) + ")";
        else if (a.modifiers.size() != b.modifiers.size())
            why = "volumes (" + std::to_string(a.modifiers.size()) + " x " + std::to_string(b.modifiers.size()) + ")";
        else if (lf.textures.size() != r_.frame.textures.size())
            why = "texturas (" + std::to_string(lf.textures.size()) + " x " + std::to_string(r_.frame.textures.size()) + ")";
        for (size_t i = 0; why.empty() && i < a.draws.size(); ++i)
            if (!(a.draws[i].state == b.draws[i].state) || a.draws[i].first_vertex != b.draws[i].first_vertex ||
                a.draws[i].vertex_count != b.draws[i].vertex_count)
                why = "desenho " + std::to_string(i);
        for (size_t i = 0; why.empty() && i < a.modifiers.size(); ++i) {
            const nr::ModifierVolume &x = a.modifiers[i], &y = b.modifiers[i];
            if (x.first_vertex != y.first_vertex || x.triangle_count != y.triangle_count || x.inclusion != y.inclusion ||
                x.list != y.list || x.cull != y.cull || x.closed != y.closed || x.or_from != y.or_from)
                why = "volume " + std::to_string(i);
        }
        for (size_t i = 0; why.empty() && i < lf.textures.size(); ++i) {
            const km::TextureKey& x = lf.textures[i];
            const TextureKey&     y = r_.frame.textures[i];
            if (x.tcw != y.tcw || x.width != y.width || x.height != y.height || x.stride != y.stride)
                why = "textura " + std::to_string(i);
        }
        if (why.empty() && (lf.modifier_params != r_.frame.modifier_params || lf.modifier_volumes != r_.frame.modifier_volumes ||
                            lf.modifier_unclosed != r_.frame.modifier_unclosed || lf.skipped_params != r_.frame.skipped_params ||
                            lf.sprites != r_.frame.sprites || lf.texture_stride != r_.frame.texture_stride))
            why = "contagens do quadro";
        // revisao Codex 20261028-173027: as contagens que decidem o erro do produto (unsupported) e as do diagnostico, contra
        // o decodificador do legado (RenderStats) e os retornos da captura deste render
        const RenderStats* ls = pvr_render_stats_in_progress();
        if (why.empty() && !ls) why = "sem as contagens do decodificador do legado";
        if (why.empty() && (lf.unsupported != ls->unsupported || lf.skipped != ls->skipped || lf.two_volume != ls->two_volume ||
                            lf.triangles != ls->triangles || lf.sprites != ls->sprites))
            why = "contagens do decodificador (suporte " + std::to_string(lf.unsupported) + " x " + std::to_string(ls->unsupported) +
                  ", omitidos " + std::to_string(lf.skipped) + " x " + std::to_string(ls->skipped) + ", dois volumes " +
                  std::to_string(lf.two_volume) + " x " + std::to_string(ls->two_volume) + ")";
        if (why.empty() && (lf.global_two_volume != r_.st.two_volume - at_begin_[0] ||
                            lf.volume_bad_instruction != r_.st.volume_bad_instruction - at_begin_[1] ||
                            lf.volume_mixed_cull != r_.st.volume_mixed_cull - at_begin_[2] ||
                            lf.volume_inclusion != r_.st.volume_inclusion - at_begin_[3] ||
                            lf.volume_exclusion != r_.st.volume_exclusion - at_begin_[4] || lf.volume_or != r_.st.volume_or - at_begin_[5]))
            why = "contagens dos volumes e dos globais";
        // os triangulos do rasterizador do legado: os dos strips e dois por sprite
        if (why.empty() && size_t(lf.triangles) + 2u * lf.sprites != drawn.size())
            why = "triangulos (" + std::to_string(lf.triangles + 2u * lf.sprites) + " x " + std::to_string(drawn.size()) + ")";
        if (why.empty()) return;
        ++r_.st.list_mismatch;
        if (r_.st.list_error.empty()) r_.st.list_error = "render " + std::to_string(r_.st.renders) + ": " + why;
    }

    void reset_strip() {
        strip_.clear();
        open_ = -1, tri_mode_ = false;
    }

    nr::DrawState state_of(const PvrTri& t) {
        uint32_t handle = 0;
        if (t.tex) {
            const TextureKey k = key_of(t, stride_);
            size_t           i = 0;
            while (i < r_.frame.textures.size() && !(r_.frame.textures[i] == k)) ++i;
            if (i == r_.frame.textures.size()) r_.frame.textures.push_back(k);
            handle = uint32_t(i + 1);
        }
        return draw_state(t, handle);
    }
};

std::unique_ptr<Capture> g_cap;

} // namespace

void connect_legacy_decoder(Renderer* r) {
    pvr_render_set_capture(nullptr);
    g_cap = r ? std::make_unique<Capture>(*r) : nullptr;
    pvr_render_set_capture(g_cap.get());
}

} // namespace bs::transitional::render_capture
