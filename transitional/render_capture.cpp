#include "render_capture.h"

#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "render/frame_prep.h"
#include "render/render_ir.h"
#include "render/texture_convert.h"
#include "render/vulkan/vk_device.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <deque>
#include <map>
#include <tuple>
#include <memory>
#include <set>
#include <utility>

namespace bs::transitional::render_capture {
namespace {

constexpr uint32_t R(uint32_t off) { return 0x005F8000 + off; }

float f32(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

uint32_t pack(const PvrColor& c) {
    auto q = [](float v) { return uint32_t(std::lround((v < 0 ? 0 : (v > 1 ? 1 : v)) * 255)); };
    return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}
PvrColor unpack(uint32_t c) {  // a mesma expressao do decodificador (x / 255.f): o round-trip e exato
    return {((c >> 24) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f};
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
uint32_t list_number(nr::ListType l) {
    switch (l) {
    case nr::ListType::OpaqueModifier: return 1;
    case nr::ListType::Translucent: return 2;
    case nr::ListType::TranslucentModifier: return 3;
    case nr::ListType::PunchThrough: return 4;
    default: return 0;
    }
}

uint32_t log2_size(uint32_t n) {  // 8 << k
    uint32_t k = 0;
    while ((8u << k) < n && k < 7) ++k;
    return k;
}

TextureKey key_of(const PvrTri& t, uint32_t stride) {
    return {t.tcw, 8u << ((t.tsp >> 3) & 7), 8u << (t.tsp & 7), stride};
}

bool same_vertex(const PvrVertex& a, const PvrVertex& b) {
    return std::memcmp(&a, &b, sizeof a) == 0;  // exato: os dois lados sao os mesmos floats
}

nr::ITextureReplacements* g_replacements = nullptr;  // texturas HD (set_texture_replacements)
Fault                     g_fault        = Fault::None;     // testes do recuo (inject_fault)
float                     g_scale        = 1.f;             // resolucao interna (set_scale)

class Capture final : public PvrCaptureSink {
public:
    Mode     mode  = Mode::Off;
    uint32_t every = 1;
    Frame    frame;
    Stats    st;

    void begin(uint32_t w, uint32_t h) override {
        frame         = {};
        frame.cl.frame = frame_params(w, h);
        stride_       = (pvr_reg(R(0x0E4)) & 0x1F) * 32;  // TEXT_CONTROL
        frame.texture_stride = stride_;
        reset_strip();
        mod_open_ = false, mod_closing_ = false, mod_cur_closed_ = false;  // os volumes recomecam a cada render
        mod_instr_ = 0, mod_closed_ = 0;
    }

    void vertex(const PvrTri& state, const PvrVertex& v, bool end) override {
        strip_.push_back(v);
        const size_t n = strip_.size();
        if (n >= 3) {
            const nr::DrawState s = state_of(state);
            const bool contiguous = open_ >= 0 && frame.cl.draws[size_t(open_)].first_vertex +
                                                          frame.cl.draws[size_t(open_)].vertex_count ==
                                                      frame.cl.vertices.size();  // os vertices do strip sao os ultimos
            if (!tri_mode_ && contiguous && s == frame.cl.draws[size_t(open_)].state) {
                frame.cl.draws[size_t(open_)].vertex_count++;  // o strip continua com o mesmo estado
                frame.cl.vertices.push_back(render_capture::vertex(v));
            } else if (!tri_mode_ && n == 3) {  // o primeiro triangulo do strip abre o desenho
                open_ = int32_t(frame.cl.draws.size());
                frame.cl.draws.push_back({s, uint32_t(frame.cl.vertices.size()), 3});
                for (size_t i = 0; i < 3; ++i) frame.cl.vertices.push_back(render_capture::vertex(strip_[i]));
            } else {
                // estado mudou no meio do strip (recorte do usuario entre vertices): daqui ate o fim do strip, um
                // desenho por triangulo, na ordem exata do decodificador (n par: os dois primeiros trocados)
                tri_mode_ = true, open_ = -1;
                PvrVertex t[3] = {strip_[n - 3], strip_[n - 2], strip_[n - 1]};
                if (n % 2 == 0) std::swap(t[0], t[1]);
                frame.cl.draws.push_back({s, uint32_t(frame.cl.vertices.size()), 3});
                for (const PvrVertex& x : t) frame.cl.vertices.push_back(render_capture::vertex(x));
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
            frame.cl.draws.push_back({s, uint32_t(frame.cl.vertices.size()), 3});
            for (int i = 0; i < 3; ++i) frame.cl.vertices.push_back(render_capture::vertex(*t[i]));
        }
        ++frame.sprites;
        open_ = -1;
    }

    void global(const TaParam& g) override {
        // volume modificador: a instrucao (ISP bits 30-29 do global) 0 = poligono do volume, 1 = ultimo poligono
        // (inclusao), 2 = ultimo poligono (exclusao). Os triangulos depois de um global 1/2 fecham o volume; o
        // proximo global (de qualquer lista) comeca outro (revisao Codex 2026-10-21: globais iguais nao se fundem)
        if (mod_closing_) mod_open_ = false;
        mod_closing_ = false;
        if (g.list != 1 && g.list != 3) return;
        mod_instr_   = (g.w[1] >> 29) & 3;
        mod_closing_ = mod_instr_ == 1 || mod_instr_ == 2;
    }

    void modifier(const TaParam&, const TaParam& p) override {
        // vertice de volume modificador (64 bytes): A, B, C em x, y, z nas palavras 1..9
        ++frame.modifier_params;
        if (!mod_open_ || frame.cl.modifiers.back().list != list_type(p.list)) {
            frame.cl.modifiers.push_back({uint32_t(frame.cl.vertices.size()), 0, true, list_type(p.list)});
            ++frame.modifier_volumes;
            mod_open_ = true, mod_cur_closed_ = false;
        }
        if (mod_closing_) {
            frame.cl.modifiers.back().inclusion = mod_instr_ == 1;
            if (!mod_cur_closed_) mod_cur_closed_ = true, ++mod_closed_;
        }
        for (int i = 0; i < 3; ++i) {
            nr::Vertex v;
            v.x = f32(p.w[1 + 3 * i]), v.y = f32(p.w[2 + 3 * i]), v.inv_w = f32(p.w[3 + 3 * i]);
            frame.cl.vertices.push_back(v);
        }
        frame.cl.modifiers.back().triangle_count++;
    }

    void skipped(const TaParam&) override { ++frame.skipped_params; }

    bool render(const PvrFrame&, uint32_t w, uint32_t h, std::vector<uint32_t>& image) override {
        static const std::pair<uint64_t, std::string> dumpAt = [] {
            const char* v = std::getenv("BS_RENDER_IR_GUARDAR");
            if (!v || !std::strchr(v, ':')) return std::pair<uint64_t, std::string>{0, {}};
            return std::pair<uint64_t, std::string>{std::strtoull(v, nullptr, 10), std::strchr(v, ':') + 1};
        }();
        if (dumpAt.first && st.renders + 1 == dumpAt.first)
            std::fprintf(stderr, "[render] IR do render %llu em %s: %s\n", static_cast<unsigned long long>(dumpAt.first),
                         dumpAt.second.c_str(), save_frame_dump(dumpAt.second, frame) ? "ok" : "FALHOU");
        if (mode != Mode::Native && mode != Mode::Compare) return false;
        vkOk_ = vk_render(w, h, mode == Mode::Native ? image : vkImage_);
        return mode == Mode::Native && vkOk_;
    }

    void end(const PvrFrame& used, std::span<const PvrTri> drawn, std::span<const uint32_t> image, uint32_t w,
             uint32_t h) override {
        frame.modifier_unclosed = frame.modifier_volumes - mod_closed_;
        st.modifier_unclosed += frame.modifier_unclosed;
        ++st.renders;
        if (mode == Mode::Compare && vkOk_) compare(image, w, h);
        st.draws += frame.cl.draws.size(), st.vertices += frame.cl.vertices.size(), st.sprites += frame.sprites;
        st.modifier_params += frame.modifier_params, st.modifier_volumes += frame.modifier_volumes;
        st.skipped_params += frame.skipped_params;
        if (mode != Mode::Check || (st.renders - 1) % every) return;
        ++st.checked;
        auto fail = [&](uint64_t& counter, const std::string& what) {
            ++counter;
            if (st.first_error.empty()) st.first_error = "render " + std::to_string(st.renders) + ": " + what;
        };
        std::string err;
        if (!valid(frame, &err)) {
            fail(st.invalid, "IR invalida: " + err);
            return;  // indices nao confiaveis: nada mais e percorrido
        }
        const PvrFrame from_ir = pvr_frame(frame);
        if (!(from_ir == used)) fail(st.frame_mismatch, "FrameParams nao reproduzem os registradores do quadro");
        const std::vector<PvrTri> tris = triangles(frame);
        st.triangles += tris.size();
        if (tris.size() != drawn.size()) {
            fail(st.triangle_mismatch, "IR com " + std::to_string(tris.size()) + " triangulos, rasterizador " +
                                           std::to_string(drawn.size()));
        } else {
            for (size_t i = 0; i < tris.size(); ++i) {
                const PvrTri& a = tris[i];
                const PvrTri& b = drawn[i];
                const bool    vtx = same_vertex(a.v[0], b.v[0]) && same_vertex(a.v[1], b.v[1]) && same_vertex(a.v[2], b.v[2]);
                const bool    est = a.list == b.list && state_of(a) == state_of(b) &&
                                 (!b.tex || key_of(a, stride_) == key_of(b, stride_));
                if (!vtx || !est) {
                    fail(st.triangle_mismatch, "triangulo " + std::to_string(i) + (vtx ? " (estado)" : " (vertices)"));
                    break;
                }
            }
        }
        const std::vector<uint32_t> again = pvr_render_triangles(tris, from_ir, w, h);
        if (again.size() != image.size() || std::memcmp(again.data(), image.data(), image.size() * 4) != 0) {
            size_t diff = 0;
            for (size_t i = 0; i < std::min(again.size(), image.size()); ++i) diff += again[i] != image[i];
            fail(st.image_mismatch, "imagem a partir da IR difere em " + std::to_string(diff) + " pixels");
        }
        for (const TextureKey& k : frame.textures) {
            std::vector<uint32_t> argb;
            if (!convert_texture(k, argb)) {
                ++st.textures_without_native;
                continue;
            }
            ++st.textures_checked;
            const nr::TextureId id = nr::texture_id(k.width, k.height, argb);
            if (keys_.insert({k.tcw, (uint64_t(k.width) << 32) | k.height}).second) ++st.distinct_texture_keys;
            if (ids_.insert({id.content, (uint64_t(id.width) << 32) | id.height}).second) ++st.distinct_texture_ids;
            const TexInfo t{k.tcw, k.width, k.height, k.stride};
            for (uint32_t y = 0, bad = 0; y < k.height && !bad; ++y)
                for (uint32_t x = 0; x < k.width; ++x)
                    if (argb[size_t(y) * k.width + x] != pvr_texel(t, x, y)) {
                        char b[96];
                        std::snprintf(b, sizeof b, "textura TCW %08X %ux%u: texel (%u,%u)", k.tcw, k.width, k.height, x, y);
                        fail(st.texture_mismatch, b);
                        bad = 1;
                        break;
                    }
        }
    }

private:
    std::vector<PvrVertex> strip_;
    int32_t                open_     = -1;
    bool                   tri_mode_ = false;
    uint32_t               stride_   = 0;
    bool                   mod_open_ = false, mod_closing_ = false, mod_cur_closed_ = false;
    uint32_t               mod_instr_ = 0, mod_closed_ = 0;  // instrucao do ultimo global; volumes com ultimo poligono
    std::set<std::pair<uint64_t, uint64_t>> keys_, ids_;  // para o resumo: quantas chaves e quantos conteudos

    // ---- N7b: backend Vulkan ----
    std::unique_ptr<nr::VulkanDevice> vk_;
    bool                              vkTried_ = false, vkOk_ = false;
    std::vector<uint32_t>             vkImage_;
    struct VkTex {
        nr::TextureHandle    handle;
        std::vector<uint8_t> raw;
        uint64_t             used = 0;
        uint32_t             w = 0, h = 0;  // o tamanho no dispositivo (o da substituicao, se houver)
        const void*          provider = nullptr;
    };
    std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, VkTex> vkTex_;
    std::vector<uint8_t>  raw_;
    std::vector<uint32_t> argb_;

    // N7c: os ultimos renders na escala (o mais novo na frente): a imagem reduzida que foi para a VRAM (para achar o
    // render exibido) e a maior. Tres por causa do buffer duplo (o exibido e o anterior ao ultimo) com folga.
    struct Hires {
        std::vector<uint32_t>                  down;
        std::shared_ptr<std::vector<uint32_t>> image;
        uint32_t                               w = 0, h = 0, iw = 0, ih = 0;
    };
    std::deque<Hires> hires_;

    // buffer para a proxima imagem maior: o do mais antigo, se ninguem (um quadro publicado) ainda o usa
    std::shared_ptr<std::vector<uint32_t>> hires_buffer() {
        if (hires_.size() >= 3) {
            std::shared_ptr<std::vector<uint32_t>> b = std::move(hires_.back().image);
            hires_.pop_back();
            if (b && b.use_count() == 1) return b;
        }
        return std::make_shared<std::vector<uint32_t>>();
    }

public:
    HiresImage hires_for(std::span<const uint32_t> shown, uint32_t w, uint32_t h) {
        if (hires_.empty()) return {};
        if (g_scale == 1.f) {
            hires_.clear();
            return {};
        }
        // o video pode mostrar so parte das linhas do render (entrelacado: 640 x 476 de um render de 640 x 480): procura a
        // linha do render em que a imagem exibida comeca (ate 8 de deslocamento)
        for (const Hires& e : hires_) {
            if (e.w != w || h > e.h || shown.size() != size_t(w) * h) continue;
            for (uint32_t dy = 0; dy <= std::min<uint32_t>(8, e.h - h); ++dy) {
                // amostra de 1 pixel em 4 nas duas direcoes; diferenca por canal <= 16 (565/555/4444 perdem ate 4 bits)
                uint32_t n = 0, bad = 0;
                for (uint32_t y = 1; y < h && bad * 1000 <= n + 1000; y += 4)
                    for (uint32_t x = 1; x < w; x += 4, ++n) {
                        const uint32_t a = shown[size_t(y) * w + x], b = e.down[size_t(y + dy) * w + x];
                        for (int s = 0; s < 24; s += 8)
                            if (std::abs(int((a >> s) & 0xFF) - int((b >> s) & 0xFF)) > 16) {
                                ++bad;
                                break;
                            }
                    }
                if (n && bad * 1000 <= n) {  // ate 0,1 % fora (bordas de recorte do framebuffer)
                    ++st.hires_shown;
                    HiresImage r{e.image, e.iw, e.ih};
                    // as linhas da imagem maior que correspondem as exibidas
                    r.y0 = uint32_t(std::lround(double(dy) * e.ih / e.h)), r.rows = uint32_t(std::lround(double(h) * e.ih / e.h));
                    r.rows = std::min(r.rows, e.ih - r.y0);
                    return r;
                }
            }
        }
        ++st.hires_missed;
        return {};
    }

    std::string dumpDir;
    uint32_t    dumpEvery = 300;
    ~Capture() override {
        if (vk_)
            for (auto& [k, t] : vkTex_) vk_->destroy_texture(t.handle);
    }

private:
    bool ensure_vk() {
        if (vk_) return true;
        if (vkTried_) return false;
        vkTried_ = true;
        vk_      = nr::VulkanDevice::create(&st.vk_error);
        if (!vk_) {
            std::fprintf(stderr, "[render] Vulkan indisponivel (%s): renderizador em software\n", st.vk_error.c_str());
            return false;
        }
        st.vk_device = vk_->stats().device;
        const auto& vs = vk_->stats();
        // garantido bit a bit com arredondamento ao par (RTE) e profundidade crua; os subnormais (abaixo de 1,2e-38) ficam
        // no padrao do dispositivo quando ele nao garante preserva-los
        std::fprintf(stderr,
                     "[render] Vulkan: %s (modo fiel bit a bit %s: arredondamento ao par %s, subnormais preservados %s, "
                     "profundidade crua %s)\n",
                     st.vk_device.c_str(), vs.float_controls && vs.raw_depth ? "garantido" : "NAO garantido",
                     vs.float_controls ? "sim" : "nao", vs.denorm_preserve ? "sim" : "padrao do dispositivo",
                     vs.raw_depth ? "sim" : "nao");
        return true;
    }

    // handle do dispositivo para a textura: refeita quando os bytes da VRAM (ou a paleta) mudam
    bool texture_handle(const TextureKey& k, nr::TextureHandle& out) {
        if (g_fault == Fault::Texture) {
            g_fault = Fault::None;
            return false;
        }
        texture_source(k, raw_);
        VkTex& e = vkTex_[{k.tcw, k.width, k.height, k.stride}];
        e.used   = st.renders;
        if (e.handle && e.raw == raw_ && e.provider == g_replacements) {
            out = e.handle;
            return true;
        }
        bool legacy = false;
        if (!texture_argb(k, argb_, legacy)) return false;
        st.tex_legacy += legacy;
        // substituicao HD pelo conteudo (nunca pelo endereco): compativel ou o original
        const std::vector<uint32_t>* texels = &argb_;
        uint32_t                     tw = k.width, th = k.height;
        if (g_replacements) {
            const nr::TextureId id = nr::texture_id(k.width, k.height, argb_);
            if (const nr::ReplacementTexture* r = g_replacements->find(id)) {
                if (nr::replacement_compatible(id, *r)) texels = &r->argb, tw = r->width, th = r->height, ++st.tex_replaced;
                else ++st.tex_replace_rejected;
            }
        }
        const auto bytes = std::as_bytes(std::span<const uint32_t>(*texels));
        if (e.handle && (e.w != tw || e.h != th)) vk_->destroy_texture(e.handle), e.handle = {};
        if (e.handle) {
            if (!vk_->update_texture(e.handle, bytes)) return false;
        } else {
            nr::TextureDesc d;
            d.width = tw, d.height = th;
            auto r = vk_->create_texture(d, bytes);
            if (!r) return false;
            e.handle = r.value();
        }
        e.w = tw, e.h = th, e.provider = g_replacements;
        e.raw = raw_;
        ++st.tex_uploads;
        out = e.handle;
        return true;
    }

    void evict() {  // texturas sem uso ha 600 renders saem da GPU
        for (auto it = vkTex_.begin(); it != vkTex_.end();)
            if (st.renders - it->second.used > 600) {
                if (it->second.handle) vk_->destroy_texture(it->second.handle);
                it = vkTex_.erase(it), ++st.tex_evicted;
            } else {
                ++it;
            }
    }

    // Perda do dispositivo (VK_ERROR_DEVICE_LOST e falhas do quadro): o dispositivo e recriado a partir dos dados da
    // CPU (as texturas voltam do cache da VRAM) e o MESMO render e refeito; ate kRecoveries por sessao. Falhou de novo:
    // software ate o fim da sessao (docs/N7-PLANO.md secao 3).
    static constexpr uint32_t kRecoveries = 3;
    bool vk_render(uint32_t w, uint32_t h, std::vector<uint32_t>& out) {
        std::string why;
        if (vk_attempt(w, h, out, &why)) return true;
        if (!vk_ && vkTried_ && why.empty()) return false;  // sem Vulkan desde o inicio
        ++st.vk_failures;
        if (why == "textura") return false;  // so este render no software (o dispositivo continua)
        if (st.vk_recoveries < kRecoveries) {
            ++st.vk_recoveries;
            std::fprintf(stderr, "[render] Vulkan: falha no render %llu (%s); recriando o dispositivo (%u de %u)\n",
                         static_cast<unsigned long long>(st.renders + 1), why.c_str(), unsigned(st.vk_recoveries), kRecoveries);
            drop_vk();
            vkTried_ = false;  // ensure_vk cria de novo
            std::string again;
            if (vk_attempt(w, h, out, &again)) return true;
            ++st.vk_failures;
            why = again.empty() ? "recriacao" : again;
        }
        st.vk_error = "falha do dispositivo Vulkan no render " + std::to_string(st.renders + 1) + " (" + why +
                      "): renderizador em software ate o fim da sessao";
        std::fprintf(stderr, "[render] %s\n", st.vk_error.c_str());
        drop_vk();
        vkTried_ = true;  // nao tenta mais
        return false;
    }

    void drop_vk() {
        for (auto& [k, t] : vkTex_) t.handle = {};
        vkTex_.clear();
        vk_.reset();
    }

    bool vk_attempt(uint32_t w, uint32_t h, std::vector<uint32_t>& out, std::string* why) {
        if (!ensure_vk()) return false;
        if (st.renders % 300 == 0) evict();
        std::vector<nr::TextureHandle> handles(frame.textures.size());
        for (size_t i = 0; i < frame.textures.size(); ++i)
            if (!texture_handle(frame.textures[i], handles[i])) {
                // sem a textura o quadro sairia errado: este render fica no software (nunca um desenho sem textura)
                if (st.vk_error.empty()) st.vk_error = "textura nao enviada ao Vulkan (render no software)";
                *why = "textura";
                return false;
            }
        const std::vector<nr::Draw> saved = frame.cl.draws;  // a IR do quadro fica com os handles do quadro
        for (nr::Draw& d : frame.cl.draws)
            if (d.state.texture) d.state.texture = handles[d.state.texture.value - 1];
        uint32_t   rw = 0, rh = 0;
        // N7c: a escala so no nativo (o modo comparar compara na resolucao do jogo)
        const float    sc = mode == Mode::Native ? g_scale : 1.f;
        const uint32_t ew = nr::scaled_size(w, sc), eh = nr::scaled_size(h, sc);
        vk_->set_scale(sc);
        std::shared_ptr<std::vector<uint32_t>> hi = sc == 1.f ? nullptr : hires_buffer();
        if (sc == 1.f) hires_.clear();
        // testes: Device falha esta tentativa e a da recuperacao; DeviceOnce so esta
        const bool injected = g_fault == Fault::Device || g_fault == Fault::DeviceOnce || g_fault == Fault::DeviceAgain;
        if (g_fault == Fault::Device) g_fault = Fault::DeviceAgain;
        else if (injected) g_fault = Fault::None;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = !injected && vk_->submit(frame.cl).ok() && vk_->read_frame(hi ? *hi : out, rw, rh).ok() && rw == ew &&
                        rh == eh;
        frame.cl.draws = saved;
        if (ok && hi) {  // o framebuffer recebe a imagem reduzida; a maior fica para a apresentacao
            nr::downsample(*hi, rw, rh, out, w, h);
            Hires e;
            e.down = out, e.image = std::move(hi), e.w = w, e.h = h, e.iw = rw, e.ih = rh;
            hires_.push_front(std::move(e));
            ++st.hires_renders, st.hires_w = rw, st.hires_h = rh;
            st.hires_cpu_ms_total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() -
                                     (vk_->stats().gpu_ms_total - st.vk_gpu_ms_total);
        }
        if (ok) {
            ++st.vk_frames;
            st.vk_fragment_retries = vk_->stats().fragment_retries, st.vk_negative_depth = vk_->stats().negative_depth_frames;
            st.vk_gpu_ms_total = vk_->stats().gpu_ms_total, st.vk_gpu_ms_max = vk_->stats().gpu_ms_max;
            st.vk_max_layers   = vk_->stats().max_layers;
            return true;
        }
        *why = injected                           ? std::string("falha injetada")
               : !vk_->stats().last_error.empty() ? vk_->stats().last_error
               : (rw != ew || rh != eh)           ? "tamanho " + std::to_string(rw) + "x" + std::to_string(rh) + " em vez de " +
                                                        std::to_string(ew) + "x" + std::to_string(eh)
                                                  : std::string("leitura do quadro");
        return false;
    }

    void compare(std::span<const uint32_t> sw, uint32_t w, uint32_t h) {
        if (vkImage_.size() != sw.size()) return;
        uint64_t diff = 0, diff8 = 0;
        uint32_t mx   = 0;
        double   se   = 0;  // erro quadratico (RGB) para o PSNR
        for (size_t i = 0; i < sw.size(); ++i) {
            if (sw[i] == vkImage_[i]) continue;
            ++diff;
            uint32_t m = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                const int d = int((sw[i] >> sh) & 0xFF) - int((vkImage_[i] >> sh) & 0xFF);
                m = std::max<uint32_t>(m, uint32_t(std::abs(d)));
                if (sh < 24) se += double(d) * d;
            }
            diff8 += m > 8, mx = std::max(mx, m);
        }
        const double mse  = se / (3.0 * double(sw.size()));
        const double psnr = mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 99.0;
        st.cmp_min_psnr   = st.cmp_frames ? std::min(st.cmp_min_psnr, psnr) : psnr;
        st.cmp_psnr_sum += psnr, st.cmp_below40 += psnr < 40.0;
        ++st.cmp_frames, st.cmp_pixels += sw.size(), st.cmp_diff += diff, st.cmp_diff8 += diff8;
        st.cmp_max = std::max(st.cmp_max, mx);
        const double frac = double(diff8) / double(sw.size());
        if (frac > st.cmp_worst_fraction) st.cmp_worst_fraction = frac, st.cmp_worst_render = st.renders;
        static const bool anyDiff = std::getenv("BS_RENDER_IR_BMP_DIFF") != nullptr;  // qualquer pixel diferente
        if (!dumpDir.empty() && (st.renders % dumpEvery == 0 || frac > 0.01 || psnr < 40.0 || (anyDiff && diff)))
            dump(sw, w, h, frac);
    }

    void dump(std::span<const uint32_t> sw, uint32_t w, uint32_t h, double frac) {
        if (dumped_ >= 200) return;  // limite de arquivos
        ++dumped_;
        char name[96];
        std::snprintf(name, sizeof name, "render_%06llu_%05.2f.bmp", static_cast<unsigned long long>(st.renders), frac * 100);
        const uint32_t W = w * 3, row = (W * 3 + 3) & ~3u;
        std::vector<uint8_t> px(size_t(row) * h);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const uint32_t a = sw[size_t(y) * w + x], b = vkImage_[size_t(y) * w + x];
                uint8_t* r = px.data() + size_t(h - 1 - y) * row;
                for (int c = 0; c < 3; ++c) {
                    const int sh = 8 * c;  // B, G, R
                    r[(x) * 3 + c]         = uint8_t((a >> sh) & 0xFF);
                    r[(w + x) * 3 + c]     = uint8_t((b >> sh) & 0xFF);
                    r[(2 * w + x) * 3 + c] = uint8_t(std::min(255, 8 * std::abs(int((a >> sh) & 0xFF) - int((b >> sh) & 0xFF))));
                }
            }
        std::filesystem::create_directories(dumpDir);
        std::ofstream f(std::filesystem::path(dumpDir) / name, std::ios::binary);
        auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        f.write("BM", 2), u32(54 + uint32_t(px.size())), u32(0), u32(54);
        u32(40), u32(W), u32(h), u16(1), u16(24), u32(0), u32(uint32_t(px.size())), u32(2835), u32(2835), u32(0), u32(0);
        f.write(reinterpret_cast<const char*>(px.data()), std::streamsize(px.size()));
    }
    uint32_t dumped_ = 0;

    void reset_strip() {
        strip_.clear();
        open_ = -1, tri_mode_ = false;
    }

    nr::DrawState state_of(const PvrTri& t) {
        uint32_t handle = 0;
        if (t.tex) {
            const TextureKey k = key_of(t, stride_);
            size_t           i = 0;
            while (i < frame.textures.size() && !(frame.textures[i] == k)) ++i;
            if (i == frame.textures.size()) frame.textures.push_back(k);
            handle = uint32_t(i + 1);
        }
        return draw_state(t, handle);
    }
};

std::unique_ptr<Capture> g_cap;

} // namespace

nr::DrawState draw_state(const PvrTri& t, uint32_t texture_handle) {
    nr::DrawState s;
    s.list          = list_type(t.list);
    s.texture.value = t.tex ? texture_handle : 0;
    s.src           = nr::Blend(t.tsp >> 29);
    s.dst           = nr::Blend((t.tsp >> 26) & 7);
    s.depth         = nr::DepthTest(t.isp >> 29);
    s.depth_write   = !((t.isp >> 26) & 1);
    s.cull          = nr::Cull((t.isp >> 27) & 3);
    s.filter = nr::Filter((t.tsp >> 13) & 3);
    s.wrap_u = ((t.tsp >> 16) & 1) ? nr::Wrap::Clamp : (((t.tsp >> 18) & 1) ? nr::Wrap::Mirror : nr::Wrap::Repeat);
    s.wrap_v = ((t.tsp >> 15) & 1) ? nr::Wrap::Clamp : (((t.tsp >> 17) & 1) ? nr::Wrap::Mirror : nr::Wrap::Repeat);
    static constexpr nr::TexEnv kEnv[4] = {nr::TexEnv::Replace, nr::TexEnv::Modulate, nr::TexEnv::Decal,
                                           nr::TexEnv::ModulateAlpha};  // decal, modulate, decal alpha, modulate alpha
    s.env              = kEnv[(t.tsp >> 6) & 3];
    s.fog              = nr::Fog((t.tsp >> 22) & 3);
    s.gouraud          = t.gouraud;
    s.use_alpha        = (t.tsp >> 20) & 1;
    s.offset_color     = t.offset;
    s.ignore_tex_alpha = (t.tsp >> 19) & 1;
    s.color_clamp      = (t.tsp >> 21) & 1;
    s.mip_bias         = uint8_t((t.tsp >> 8) & 0xF);
    s.supersample      = (t.tsp >> 12) & 1;
    s.src_select       = (t.tsp >> 25) & 1;
    s.dst_select       = (t.tsp >> 24) & 1;
    s.clip             = t.clipMode == 2 ? nr::Clip::Inside : (t.clipMode == 3 ? nr::Clip::Outside : nr::Clip::None);
    if (s.clip != nr::Clip::None) s.clip_x0 = t.cx0, s.clip_y0 = t.cy0, s.clip_x1 = t.cx1, s.clip_y1 = t.cy1;
    return s;
}

PvrTri pvr_state(const nr::DrawState& s, const TextureKey* tex) {
    PvrTri t;
    t.list = list_number(s.list);
    t.isp  = (uint32_t(s.depth) << 29) | (uint32_t(s.cull) << 27) | (uint32_t(!s.depth_write) << 26);
    const uint32_t flt = uint32_t(s.filter);
    uint32_t       env = 0;
    switch (s.env) {
    case nr::TexEnv::Replace: env = 0; break;
    case nr::TexEnv::Modulate: env = 1; break;
    case nr::TexEnv::Decal: env = 2; break;
    case nr::TexEnv::ModulateAlpha: env = 3; break;
    }
    t.tsp = (uint32_t(s.src) << 29) | (uint32_t(s.dst) << 26) | (uint32_t(s.fog) << 22) | (uint32_t(s.color_clamp) << 21) |
            (uint32_t(s.use_alpha) << 20) | (uint32_t(s.ignore_tex_alpha) << 19) |
            (uint32_t(s.wrap_u == nr::Wrap::Mirror) << 18) | (uint32_t(s.wrap_v == nr::Wrap::Mirror) << 17) |
            (uint32_t(s.wrap_u == nr::Wrap::Clamp) << 16) | (uint32_t(s.wrap_v == nr::Wrap::Clamp) << 15) | (flt << 13) |
            (uint32_t(s.src_select) << 25) | (uint32_t(s.dst_select) << 24) | (uint32_t(s.supersample) << 12) |
            (uint32_t(s.mip_bias & 0xF) << 8) | (env << 6);
    t.tex = bool(s.texture) && tex;
    if (t.tex) t.tcw = tex->tcw, t.tsp |= (log2_size(tex->width) << 3) | log2_size(tex->height);
    t.offset   = s.offset_color;
    t.gouraud  = s.gouraud;
    t.clipMode = s.clip == nr::Clip::Inside ? 2 : (s.clip == nr::Clip::Outside ? 3 : 0);
    if (s.clip != nr::Clip::None) t.cx0 = s.clip_x0, t.cy0 = s.clip_y0, t.cx1 = s.clip_x1, t.cy1 = s.clip_y1;
    return t;
}

nr::Vertex vertex(const PvrVertex& v) {
    nr::Vertex o;
    o.x = v.x, o.y = v.y, o.inv_w = v.z, o.u = v.u, o.v = v.v;
    o.color = pack(v.col), o.offset = pack(v.off);
    return o;
}

PvrVertex pvr_vertex(const nr::Vertex& v) {
    PvrVertex o;
    o.x = v.x, o.y = v.y, o.z = v.inv_w, o.u = v.u, o.v = v.v;
    o.col = unpack(v.color), o.off = unpack(v.offset);
    return o;
}

nr::FrameParams frame_params(uint32_t width, uint32_t height) {
    nr::FrameParams f;
    f.width = width, f.height = height;
    f.clear_color = 0xFF000000 | pvr_background_color();
    f.clear_depth = f32(pvr_reg(R(0x088)));  // ISP_BACKGND_D
    for (size_t i = 0; i < f.fog_table.size(); ++i) f.fog_table[i] = uint16_t(pvr_reg(R(0x200 + uint32_t(i) * 4)) & 0xFFFF);
    f.fog_color             = pvr_reg(R(0x0B0));  // FOG_COL_RAM
    f.fog_vertex_color      = pvr_reg(R(0x0B4));  // FOG_COL_VERT
    f.fog_density           = pvr_reg(R(0x0B8)) & 0xFFFF;  // FOG_DENSITY
    f.punch_through_ref     = float(pvr_reg(R(0x11C)) & 0xFF) / 255.f;
    f.translucent_autosort  = !(pvr_reg(R(0x098)) & 1);  // ISP_FEED_CFG bit 0
    const uint32_t half     = pvr_reg(R(0x080));           // HALF_OFFSET
    f.sample_center         = half & 1, f.attribute_center = half & 2, f.texel_center = half & 4;
    f.small_cull            = f32(pvr_reg(R(0x078)));      // FPU_CULL_VAL
    f.clamp_max             = pvr_reg(R(0x0BC)), f.clamp_min = pvr_reg(R(0x0C0));
    return f;
}

PvrFrame pvr_frame(const Frame& f) {
    const nr::FrameParams& p = f.cl.frame;
    PvrFrame               r;
    r.background       = p.clear_color & 0xFFFFFF;
    r.background_depth = p.clear_depth;
    r.cull_val         = p.small_cull;
    r.tex_stride       = f.texture_stride;
    r.pt_ref           = uint32_t(std::lround(p.punch_through_ref * 255));  // byte / 255.f: a volta e exata
    r.half_offset      = (p.sample_center ? 1u : 0u) | (p.attribute_center ? 2u : 0u) | (p.texel_center ? 4u : 0u);
    r.clamp_max = p.clamp_max, r.clamp_min = p.clamp_min;
    r.autosort  = p.translucent_autosort;
    return r;
}

bool valid(const Frame& f, std::string* error) {
    if (!nr::validate(f.cl, error)) return false;
    for (size_t i = 0; i < f.cl.draws.size(); ++i)
        if (f.cl.draws[i].state.texture.value > f.textures.size()) {
            if (error) *error = "desenho " + std::to_string(i) + ": textura fora da tabela do quadro";
            return false;
        }
    return true;
}

std::vector<PvrTri> triangles(const Frame& f) {
    std::vector<PvrTri> out;
    for (const nr::Draw& d : f.cl.draws) {
        const TextureKey* tex = d.state.texture ? &f.textures[d.state.texture.value - 1] : nullptr;
        const PvrTri      base = pvr_state(d.state, tex);
        for (uint32_t k = 0; k + 3 <= d.vertex_count; ++k) {
            PvrTri t = base;
            for (uint32_t i = 0; i < 3; ++i) t.v[i] = pvr_vertex(f.cl.vertices[d.first_vertex + k + i]);
            if (k & 1) std::swap(t.v[0], t.v[1]);  // strip: os triangulos alternam o sentido
            out.push_back(t);
        }
    }
    return out;
}

bool convert_texture(const TextureKey& k, std::vector<uint32_t>& argb) {
    const uint32_t fmt = (k.tcw >> 27) & 7, base = (k.tcw & 0x1FFFFF) << 3;
    const bool     mip = k.tcw >> 31, vq = (k.tcw >> 30) & 1, linear = (k.tcw >> 26) & 1, strideSel = (k.tcw >> 25) & 1;
    const uint32_t w = k.width, h = k.height;
    auto           read = [&](uint32_t n) {
        std::vector<uint8_t> b(n);
        pvr_texture_read(base, b.data(), n);
        return b;
    };
    if (fmt <= 2) {
        const auto f = nr::Texel16(fmt);
        if (vq) {
            const auto d = read(2048 + (mip ? pvr_mip_offset(fmt, true, w) : 0) + (w / 2) * (h / 2));
            return nr::convert_vq(d, w, h, f, mip, 256, argb);
        }
        if (mip) {
            const auto                  d = read(pvr_mip_offset(fmt, false, w) + 2 * w * w);
            std::vector<nr::MipLevel>   levels;
            if (!nr::convert16_mips(d, w, f, levels) || levels.empty()) return false;
            argb = std::move(levels[0].argb);
            return true;
        }
        const uint32_t rowW = linear && strideSel && k.stride ? k.stride : w;  // linear com stride select
        const auto     d    = read(2 * (linear ? rowW * h : w * h));
        std::vector<uint16_t> px(size_t(w) * h);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const size_t from = linear ? size_t(y) * rowW + x : size_t(y) * w + x;  // twiddled: o vetor inteiro
                std::memcpy(&px[size_t(y) * w + x], d.data() + 2 * from, 2);
            }
        return nr::convert16(px, w, h, f, !linear, argb);
    }
    if (fmt == 5 && !mip) {  // paleta de 4 bits: o banco do seletor (TCW bits 26-21)
        std::vector<uint32_t> pal(16);
        const uint32_t        sel = (k.tcw >> 21) & 0x3F;
        for (uint32_t i = 0; i < 16; ++i) pal[i] = pvr_pal_entry(sel * 16 + i);
        return nr::convert_pal4(read(w * h / 2), w, h, pal, argb);
    }
    return false;  // paleta de 8 bits, YUV, bump: sem conversao nativa (o jogo nao os usa nos arquivos)
}

void texture_source(const TextureKey& k, std::vector<uint8_t>& raw) {
    const uint32_t fmt = (k.tcw >> 27) & 7, base = (k.tcw & 0x1FFFFF) << 3;
    const bool     mip = k.tcw >> 31, vq = (k.tcw >> 30) & 1, linear = (k.tcw >> 26) & 1, strideSel = (k.tcw >> 25) & 1;
    const uint32_t w = k.width, h = k.height;
    const uint32_t bits = fmt == 5 ? 4 : (fmt == 6 ? 8 : 16);  // paleta de 4 e de 8 bits; o resto, 16
    uint32_t       n;
    if (vq) n = 2048 + (mip ? pvr_mip_offset(fmt, true, w) : 0) + (w / 2) * (h / 2);
    else if (linear && strideSel && k.stride) n = 2 * k.stride * h;
    else if (mip) n = pvr_mip_offset(fmt, false, w) + bits * w * w / 8;
    else n = bits * w * h / 8;
    raw.resize(n);
    pvr_texture_read(base, raw.data(), n);
    if (fmt == 5 || fmt == 6) {  // o banco de paleta que o seletor escolhe
        const uint32_t first = fmt == 5 ? ((k.tcw >> 21) & 0x3F) * 16 : ((k.tcw >> 25) & 3) * 256, count = fmt == 5 ? 16 : 256;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t e = pvr_pal_entry(first + i);
            raw.insert(raw.end(), reinterpret_cast<const uint8_t*>(&e), reinterpret_cast<const uint8_t*>(&e) + 4);
        }
    }
}

bool texture_argb(const TextureKey& k, std::vector<uint32_t>& argb, bool& legacy) {
    legacy = false;
    if (convert_texture(k, argb)) return true;
    // sem conversao nativa (paleta de 8 bits, YUV, bump): o decodificador do legado, o mesmo que o software amostra
    // (so chegam aqui formatos que ele aceita: os outros nem viram desenho)
    const TexInfo t{k.tcw, k.width, k.height, k.stride};
    argb.resize(size_t(k.width) * k.height);
    for (uint32_t y = 0; y < k.height; ++y)
        for (uint32_t x = 0; x < k.width; ++x) argb[size_t(y) * k.width + x] = pvr_texel(t, x, y);
    legacy = true;
    return true;
}

bool content_id(const TextureKey& k, nr::TextureId& id) {
    std::vector<uint32_t> argb;
    if (!convert_texture(k, argb)) return false;
    id = nr::texture_id(k.width, k.height, argb);
    return true;
}

void set_texture_replacements(nr::ITextureReplacements* provider) { g_replacements = provider; }

void  set_scale(float s) { g_scale = s > 0 ? s : 1.f; }
float scale() { return g_scale; }

HiresImage hires_for(std::span<const uint32_t> shown, uint32_t width, uint32_t height) {
    return g_cap ? g_cap->hires_for(shown, width, height) : HiresImage{};
}

namespace {
constexpr char kDumpMagic[8] = {'B', 'S', 'I', 'R', '1', 0, 0, 0};
template <class T> void put(std::ofstream& o, const T& v) { o.write(reinterpret_cast<const char*>(&v), sizeof v); }
template <class T> void put_vec(std::ofstream& o, const std::vector<T>& v) {
    put(o, uint32_t(v.size()));
    if (!v.empty()) o.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size() * sizeof(T)));
}
template <class T> bool get(std::ifstream& i, T& v) { return bool(i.read(reinterpret_cast<char*>(&v), sizeof v)); }
template <class T> bool get_vec(std::ifstream& i, std::vector<T>& v) {
    uint32_t n = 0;
    if (!get(i, n) || n > (1u << 28)) return false;
    v.resize(n);
    return n == 0 || bool(i.read(reinterpret_cast<char*>(v.data()), std::streamsize(size_t(n) * sizeof(T))));
}
} // namespace

bool save_frame_dump(const std::string& path, const Frame& f) {
    std::ofstream o(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write(kDumpMagic, 8);
    put(o, f.cl.frame);
    put_vec(o, f.cl.vertices), put_vec(o, f.cl.draws), put_vec(o, f.cl.modifiers);
    put(o, uint32_t(f.textures.size()));
    for (const TextureKey& k : f.textures) {
        std::vector<uint32_t> argb;
        bool                  legacy = false;
        if (!texture_argb(k, argb, legacy)) argb.assign(size_t(k.width) * k.height, 0xFFFF00FFu);
        put(o, k.width), put(o, k.height), put_vec(o, argb);
    }
    return bool(o);
}

bool load_frame_dump(const std::string& path, nr::CommandList& cl, std::vector<DumpTexture>& textures) {
    std::ifstream i(std::filesystem::path(path), std::ios::binary);
    char          magic[8];
    if (!i.read(magic, 8) || std::memcmp(magic, kDumpMagic, 8) != 0) return false;
    if (!get(i, cl.frame) || !get_vec(i, cl.vertices) || !get_vec(i, cl.draws) || !get_vec(i, cl.modifiers)) return false;
    uint32_t n = 0;
    if (!get(i, n) || n > 65536) return false;
    textures.resize(n);
    for (DumpTexture& t : textures) {
        if (!get(i, t.desc.width) || !get(i, t.desc.height) || !get_vec(i, t.argb)) return false;
        t.desc.format = nr::PixelFormat::ARGB8888;
    }
    return true;
}
void inject_fault(Fault f) { g_fault = f; }

void set_compare_dump(const std::string& dir, uint32_t every) {
    if (!g_cap) g_cap = std::make_unique<Capture>();
    g_cap->dumpDir = dir, g_cap->dumpEvery = every ? every : 300;
}

void install(Mode m, uint32_t every) {
    if (m == Mode::Off) {  // desinstala e descarta (estatisticas, quadro, dispositivo Vulkan)
        pvr_render_set_capture(nullptr);
        g_cap.reset();
        return;
    }
    std::string dir = g_cap ? g_cap->dumpDir : std::string();
    uint32_t    dumpEvery = g_cap ? g_cap->dumpEvery : 300;
    g_cap        = std::make_unique<Capture>();  // estatisticas, quadro e conjuntos de ids recomecam
    g_cap->dumpDir = dir, g_cap->dumpEvery = dumpEvery;
    g_cap->mode  = m;
    g_cap->every = every ? every : 1;
    pvr_render_set_capture(g_cap.get());
}

const Frame& last_frame() {
    static const Frame empty;
    return g_cap ? g_cap->frame : empty;
}

const Stats& stats() {
    static const Stats empty;
    return g_cap ? g_cap->st : empty;
}

std::string summary() {
    if (!g_cap) return {};
    const Stats& s = g_cap->st;
    char         b[1024];
    std::snprintf(b, sizeof b,
                  "[render-ir] renders %llu (conferidos %llu): desenhos %llu, vertices %llu, triangulos %llu, sprites %llu; "
                  "IR invalida %llu, triangulos divergentes %llu, imagens divergentes %llu; texturas conferidas %llu "
                  "(divergentes %llu, sem conversao nativa %llu; chaves distintas %llu, conteudos distintos %llu); quadros "
                  "divergentes %llu; volumes modificadores %llu (%llu triangulos, sem fechamento %llu), parametros "
                  "omitidos %llu%s%s\n",
                  (unsigned long long)s.renders, (unsigned long long)s.checked, (unsigned long long)s.draws,
                  (unsigned long long)s.vertices, (unsigned long long)s.triangles, (unsigned long long)s.sprites,
                  (unsigned long long)s.invalid, (unsigned long long)s.triangle_mismatch, (unsigned long long)s.image_mismatch,
                  (unsigned long long)s.textures_checked, (unsigned long long)s.texture_mismatch,
                  (unsigned long long)s.textures_without_native, (unsigned long long)s.distinct_texture_keys,
                  (unsigned long long)s.distinct_texture_ids, (unsigned long long)s.frame_mismatch,
                  (unsigned long long)s.modifier_volumes, (unsigned long long)s.modifier_params,
                  (unsigned long long)s.modifier_unclosed, (unsigned long long)s.skipped_params,
                  s.first_error.empty() ? "" : "; primeira: ", s.first_error.c_str());
    std::string out = b;
    if (!s.vk_device.empty() || !s.vk_error.empty()) {
        char v[900];
        std::snprintf(v, sizeof v,
                      "[render-vk] %s: quadros %llu, falhas %llu; texturas enviadas %llu (pelo decodificador do legado %llu, "
                      "descartadas do cache %llu, HD %llu, HD incompativeis %llu); dispositivo recriado %llu vez(es); comparados %llu: pixels diferentes %.4f%% (> 8 niveis %.4f%%), maior "
                      "diferenca %u, pior render %llu (%.3f%%); PSNR minimo %.1f dB, medio %.1f dB, quadros abaixo de 40 dB %llu; GPU por "
                      "quadro: media %.2f ms, pior %.1f ms; camadas translucidas por pixel ate %llu%s%s\n",
                      s.vk_device.empty() ? "indisponivel" : s.vk_device.c_str(), (unsigned long long)s.vk_frames,
                      (unsigned long long)s.vk_failures, (unsigned long long)s.tex_uploads, (unsigned long long)s.tex_legacy,
                      (unsigned long long)s.tex_evicted, (unsigned long long)s.tex_replaced,
                      (unsigned long long)s.tex_replace_rejected, (unsigned long long)s.vk_recoveries,
                      (unsigned long long)s.cmp_frames,
                      s.cmp_pixels ? 100.0 * double(s.cmp_diff) / double(s.cmp_pixels) : 0.0,
                      s.cmp_pixels ? 100.0 * double(s.cmp_diff8) / double(s.cmp_pixels) : 0.0, s.cmp_max,
                      (unsigned long long)s.cmp_worst_render, 100.0 * s.cmp_worst_fraction, s.cmp_frames ? s.cmp_min_psnr : 0.0,
                      s.cmp_frames ? s.cmp_psnr_sum / double(s.cmp_frames) : 0.0, (unsigned long long)s.cmp_below40,
                      s.vk_frames ? s.vk_gpu_ms_total / double(s.vk_frames) : 0.0, s.vk_gpu_ms_max,
                      (unsigned long long)s.vk_max_layers,
                      s.vk_error.empty() ? "" : "; ",
                      s.vk_error.c_str());
        out += v;
    }
    if (s.hires_renders) {  // N7c: resolucao interna
        char r[300];
        std::snprintf(r, sizeof r,
                      "[render-escala] alvo %ux%u: renders %llu; VBlanks com a imagem maior %llu, sem %llu; leitura de volta + "
                      "reducao na CPU: media %.2f ms por render\n",
                      s.hires_w, s.hires_h, (unsigned long long)s.hires_renders, (unsigned long long)s.hires_shown,
                      (unsigned long long)s.hires_missed, s.hires_cpu_ms_total / double(s.hires_renders));
        out += r;
    }
    return out;
}

} // namespace bs::transitional::render_capture
