// N8.7 5a (docs/N8-7-KAMUI.md): o renderizador sobre a IR, nos dois builds: o dispositivo (Vulkan ou software), o cache
// das texturas, a resolucao interna, as estatisticas e o resumo. No produto ele desenha a IR que o ListSink decodificou
// (PvrListRenderer); na validacao o adaptador do decodificador do legado (render_capture.cpp) monta a IR e o chama.
#include "render_core.h"

#include "debug/gfx_probe.h"
#include "pvr/pvr_regs.h"
#include "pvr/pvr_texture.h"
#include "pvr/ta.h"
#include "memory/sh4_dmac.h"
#include "pvr/holly_intc.h"
#include "pvr/pvr_video.h"
#include "pvr/vram.h"
#include "render/render_ir.h"
#include "render/texture_convert.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

namespace bs::transitional::render_capture {
namespace {

nr::ITextureReplacements* g_replacements = nullptr;  // texturas HD (set_texture_replacements)
Fault                     g_fault        = Fault::None;     // testes do recuo (inject_fault)
float                     g_scale        = 1.f;             // resolucao interna (set_scale)

std::unique_ptr<Renderer> g_core;

} // namespace

Renderer::~Renderer() { drop_vk(false); }

bool Renderer::draw(uint32_t w, uint32_t h, std::vector<uint32_t>& image) {
    static const std::pair<uint64_t, std::string> dumpAt = [] {
        const char* v = std::getenv("BS_RENDER_IR_GUARDAR");
        if (!v || !std::strchr(v, ':')) return std::pair<uint64_t, std::string>{0, {}};
        return std::pair<uint64_t, std::string>{std::strtoull(v, nullptr, 10), std::strchr(v, ':') + 1};
    }();
    if (dumpAt.first && st.renders + 1 == dumpAt.first)
        std::fprintf(stderr, "[render] IR do render %llu em %s: %s\n", static_cast<unsigned long long>(dumpAt.first),
                     dumpAt.second.c_str(), save_frame_dump(dumpAt.second, frame) ? "ok" : "FALHOU");
    // N8.7: o renderizador em software desenha a IR (soft_device), tambem como recuo do Vulkan (o motivo da queda ja
    // foi registrado por vk_render)
    if (g_gfx_probe)  // N8.7 fase 0: render para textura
        for (const TextureKey& k : frame.textures) gfx_probe_texture(k.tcw, k.width, k.height);
    if (mode == Mode::Software) return soft_render(w, h, image);
    if (mode != Mode::Native && mode != Mode::Compare) return false;
    vkOk_ = vk_render(w, h, mode == Mode::Native ? image : vkImage_);
    if (mode == Mode::Native && !vkOk_) return soft_render(w, h, image);
    return mode == Mode::Native && vkOk_;
}

// N8.7 (produto): a IR do ListSink (as listas do Kamui decodificadas direto) substitui a montagem pelos retornos do
// decodificador do legado; o resto (dispositivo, texturas, estatisticas) e o mesmo
bool Renderer::render_list(const bs::native::render::kamui::ListFrame& lf, uint32_t w, uint32_t h,
                           std::vector<uint32_t>& image) {
    frame                   = {};
    frame.cl                = lf.cl;
    frame.textures          = lf.textures;
    frame.texture_stride    = lf.texture_stride;
    frame.modifier_params   = lf.modifier_params, frame.modifier_volumes = lf.modifier_volumes;
    frame.modifier_unclosed = lf.modifier_unclosed, frame.skipped_params = lf.skipped_params;
    frame.sprites           = lf.sprites;
    // o que os retornos global/modifier contavam durante a decodificacao
    st.two_volume += lf.global_two_volume, st.volume_bad_instruction += lf.volume_bad_instruction;
    st.volume_mixed_cull += lf.volume_mixed_cull, st.volume_inclusion += lf.volume_inclusion;
    st.volume_exclusion += lf.volume_exclusion, st.volume_or += lf.volume_or;
    const bool ok = draw(w, h, image);
    st.modifier_unclosed += frame.modifier_unclosed;
    ++st.renders;
    account();
    return ok;
}

// as estatisticas de cada render (desenhos, fog, sombra, volumes)
void Renderer::account() {
    st.draws += frame.cl.draws.size(), st.vertices += frame.cl.vertices.size(), st.sprites += frame.sprites;
    st.modifier_params += frame.modifier_params, st.modifier_volumes += frame.modifier_volumes;
    st.skipped_params += frame.skipped_params;
    const nr::FrameParams& fp = frame.cl.frame;
    for (const nr::Draw& d : frame.cl.draws) {
        const size_t fog = size_t(d.state.fog);
        if (fog < st.fog_draws.size()) {
            ++st.fog_draws[fog];
            if (!st.fog_first_render[fog]) {
                st.fog_first_render[fog] = st.renders;
                if (d.state.fog == nr::Fog::Table) {
                    st.fog_density_first = fp.fog_density, st.fog_color_first = fp.fog_color;
                    for (const uint16_t e : fp.fog_table)
                        st.fog_table_nonzero_first += e != 0,
                            st.fog_table_max_first = std::max<uint32_t>({st.fog_table_max_first, uint32_t(e >> 8), uint32_t(e & 0xFF)});
                }
            }
        }
        if (d.state.shadow) {
            ++st.shadow_draws;
            if (!st.shadow_first_render) st.shadow_first_render = st.renders, st.shadow_scale_first = fp.shadow_scale;
        }
    }
    for (const nr::ModifierVolume& m : frame.cl.modifiers) st.translucent_volumes += m.list == nr::ListType::TranslucentModifier;
    if (!frame.cl.modifiers.empty() && !st.modifier_first_render) st.modifier_first_render = st.renders;
}

HiresImage Renderer::hires_for(std::span<const uint32_t> shown, uint32_t w, uint32_t h) {
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
                HiresImage r{e.seq, e.iw, e.ih};
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

bool Renderer::read_hires(const HiresImage& h, std::vector<uint32_t>& argb, uint32_t& w) {
    return vk_ && h.seq && vk_->read_slot(h.seq, h.y0, h.rows, argb, w);
}

bool Renderer::ensure_vk() {
    if (vk_) return true;
    if (vkTried_) return false;
    vkTried_ = true;
    vk_      = nr::vulkan_shared(&st.vk_error);
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

// handle do dispositivo para a textura: refeita quando os bytes da VRAM (ou a paleta) mudam. hd: as substituicoes HD
// valem (so no Vulkan; o software amostra o original)
bool Renderer::texture_handle(nr::IRenderDevice& dev, TexCache& cache, bool hd, const TextureKey& k,
                              nr::TextureHandle& out) {
    if (g_fault == Fault::Texture) {
        g_fault = Fault::None;
        return false;
    }
    VkTex& e = cache[{k.tcw, k.width, k.height, k.stride}];
    e.used   = st.renders;
    nr::ITextureReplacements* repl = hd ? g_replacements : nullptr;
    // N8.7 3b (docs/N8-7-KAMUI.md): a memoria das texturas diz se os bytes da textura (ou a paleta) mudaram desde a
    // ultima leitura, sem reler; na validacao os bytes ainda sao relidos e uma textura velha e contada (tem de ser 0)
    const uint64_t gen = texture_generation(k), pal = texture_palette(k) ? pvr_palette_generation() : 0;
    if (e.handle && e.gen == gen && e.pal == pal && e.provider == repl) {
#if BS_VALIDACAO
        texture_source(k, raw_);
        if (raw_ != e.raw) {
            ++st.tex_stale;
            if (st.tex_stale_error.empty())
                st.tex_stale_error = "render " + std::to_string(st.renders + 1) + ": TCW " + std::to_string(k.tcw);
        }
#endif
        out = e.handle;
        return true;
    }
    texture_source(k, raw_);
#if BS_VALIDACAO
    // negativo (BS_TEXTURE_CACHE_MUTACAO=antecipa): a geracao confirmada antes do envio, como antes da correcao
    static const bool early = [] {
        const char* v = std::getenv("BS_TEXTURE_CACHE_MUTACAO");
        return v && std::string(v) == "antecipa";
    }();
    if (early) e.gen = gen, e.pal = pal;
#endif
    if (e.handle && e.raw == raw_ && e.provider == repl) {  // os bytes nao mudaram: a geracao nova vale para o handle
        e.gen = gen, e.pal = pal;
        out = e.handle;
        return true;
    }
    bool legacy = false;
    if (!texture_argb(k, argb_, legacy)) return false;
    st.tex_legacy += legacy;
    // substituicao HD pelo conteudo (nunca pelo endereco): compativel ou o original
    const std::vector<uint32_t>* texels = &argb_;
    uint32_t                     tw = k.width, th = k.height;
    if (repl) {
        const nr::TextureId id = nr::texture_id(k.width, k.height, argb_);
        if (const nr::ReplacementTexture* r = repl->find(id)) {
            if (nr::replacement_compatible(id, *r)) texels = &r->argb, tw = r->width, th = r->height, ++st.tex_replaced;
            else ++st.tex_replace_rejected;
        }
    }
    const auto bytes = std::as_bytes(std::span<const uint32_t>(*texels));
    if (e.handle && (e.w != tw || e.h != th)) dev.destroy_texture(e.handle), e.handle = {};
    if (g_fault == Fault::TextureUpload) {  // testes: o envio falha depois da leitura dos bytes
        g_fault = Fault::None;
        return false;
    }
    if (e.handle) {
        if (!dev.update_texture(e.handle, bytes)) return false;
    } else {
        nr::TextureDesc d;
        d.width = tw, d.height = th;
        auto r = dev.create_texture(d, bytes);
        if (!r) return false;
        e.handle = r.value();
    }
    e.w = tw, e.h = th, e.provider = repl;
    // revisao Codex 20261029-013355: os bytes e as geracoes so valem depois do envio que deu certo
    e.raw = raw_, e.gen = gen, e.pal = pal;
    ++st.tex_uploads;
    out = e.handle;
    return true;
}

// texturas sem uso ha 600 renders saem do dispositivo
void Renderer::evict(nr::IRenderDevice& dev, TexCache& cache) {
    for (auto it = cache.begin(); it != cache.end();)
        if (st.renders - it->second.used > 600) {
            if (it->second.handle) dev.destroy_texture(it->second.handle);
            it = cache.erase(it), ++st.tex_evicted;
        } else {
            ++it;
        }
}

// N8.7: o renderizador em software sobre a IR (o recuo permanente do Vulkan): as texturas do quadro convertidas (sem as
// substituicoes HD) e a IR com os handles dele. Falha = erro registrado (nunca um quadro sem textura).
bool Renderer::soft_render(uint32_t w, uint32_t h, std::vector<uint32_t>& out) {
    if (!soft_) soft_ = std::make_unique<soft::SoftwareDevice>();
    if (st.renders % 300 == 0) evict(*soft_, softTex_);
    std::vector<nr::TextureHandle> handles(frame.textures.size());
    for (size_t i = 0; i < frame.textures.size(); ++i)
        if (!texture_handle(*soft_, softTex_, false, frame.textures[i], handles[i])) return soft_fail("textura");
    const std::vector<nr::Draw> saved = frame.cl.draws;
    for (nr::Draw& d : frame.cl.draws)
        if (d.state.texture) d.state.texture = handles[d.state.texture.value - 1];
    const auto r  = soft_->submit(frame.cl);
    frame.cl.draws = saved;
    uint32_t rw = 0, rh = 0;
    if (!r) return soft_fail(std::string("quadro (") + native::error_name(r.error()) + ")");
    if (!soft_->read_frame(out, rw, rh) || rw != w || rh != h) return soft_fail("tamanho da imagem");
    ++st.soft_frames;
    return true;
}
bool Renderer::soft_fail(const std::string& why) {
    ++st.soft_failures;
    if (st.soft_error.empty()) {
        st.soft_error = "render " + std::to_string(st.renders + 1) + ": " + why;
        std::fprintf(stderr, "[render] software sobre a IR: falha no %s\n", st.soft_error.c_str());
    }
    return false;
}

// Perda do dispositivo (VK_ERROR_DEVICE_LOST e falhas do quadro): o dispositivo e recriado a partir dos dados da
// CPU (as texturas voltam do cache da VRAM) e o MESMO render e refeito; ate kRecoveries por sessao. Falhou de novo:
// software ate o fim da sessao (docs/N7-PLANO.md secao 3).
bool Renderer::vk_render(uint32_t w, uint32_t h, std::vector<uint32_t>& out) {
    std::string why;
    if (vk_attempt(w, h, out, &why)) return true;
    if (!vk_ && vkTried_ && why.empty()) return false;  // sem Vulkan desde o inicio
    ++st.vk_failures;
    if (why == "textura") return false;  // so este render no software (o dispositivo continua)
    if (st.vk_recoveries < kRecoveries) {
        ++st.vk_recoveries;
        std::fprintf(stderr, "[render] Vulkan: falha no render %llu (%s); recriando o dispositivo (%u de %u)\n",
                     static_cast<unsigned long long>(st.renders + 1), why.c_str(), unsigned(st.vk_recoveries), kRecoveries);
        drop_vk(true);
        vkTried_ = false;  // ensure_vk cria de novo
        std::string again;
        if (vk_attempt(w, h, out, &again)) return true;
        ++st.vk_failures;
        why = again.empty() ? "recriacao" : again;
    }
    st.vk_error = "falha do dispositivo Vulkan no render " + std::to_string(st.renders + 1) + " (" + why +
                  "): renderizador em software ate o fim da sessao";
    std::fprintf(stderr, "[render] %s\n", st.vk_error.c_str());
    drop_vk(true);
    vkTried_ = true;  // nao tenta mais
    return false;
}

// lost = o dispositivo se perdeu: o compartilhado e descartado (o proximo e outro); senao as texturas deste uso saem
// dele (a apresentacao pode continuar com ele)
void Renderer::drop_vk(bool lost) {
    if (vk_ && !lost) {
        vk_->set_scale(1.f);
        for (auto& [k, t] : vkTex_)
            if (t.handle) vk_->destroy_texture(t.handle);
    }
    if (vk_ && lost) nr::vulkan_shared_lost(vk_.get());
    vkTex_.clear();
    hires_.clear();
    vk_.reset();
}

bool Renderer::vk_attempt(uint32_t w, uint32_t h, std::vector<uint32_t>& out, std::string* why) {
    if (!ensure_vk()) return false;
    if (st.renders % 300 == 0) evict(*vk_, vkTex_);
    std::vector<nr::TextureHandle> handles(frame.textures.size());
    for (size_t i = 0; i < frame.textures.size(); ++i)
        if (!texture_handle(*vk_, vkTex_, true, frame.textures[i], handles[i])) {
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
    vk_->set_scale(sc);
    if (sc == 1.f) hires_.clear();
    // testes: Device falha esta tentativa e a da recuperacao; DeviceOnce so esta
    const bool injected = g_fault == Fault::Device || g_fault == Fault::DeviceOnce || g_fault == Fault::DeviceAgain;
    if (g_fault == Fault::Device) g_fault = Fault::DeviceAgain;
    else if (injected) g_fault = Fault::None;
    const auto t0 = std::chrono::steady_clock::now();
    const bool submitted = !injected && vk_->submit(frame.cl).ok();
    const auto t1        = std::chrono::steady_clock::now();
    // a leitura de volta e sempre na resolucao do jogo (com a escala, a reducao feita na GPU)
    const bool ok = submitted && vk_->read_frame(out, rw, rh).ok() && rw == w && rh == h;
    frame.cl.draws = saved;
    uint32_t       iw = 0, ih = 0;
    const uint64_t seq = ok && sc != 1.f ? vk_->last_slot(iw, ih) : 0;
    if (ok && sc != 1.f) {
        ++st.hires_renders, st.hires_w = iw, st.hires_h = ih;
        st.hires_cpu_ms_total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    }
    if (seq) {  // a imagem maior ficou num slot da GPU (para a apresentacao direta)
        Hires e;
        e.down = out, e.seq = seq, e.w = w, e.h = h, e.iw = iw, e.ih = ih;
        hires_.push_front(std::move(e));
        if (hires_.size() > 3) hires_.pop_back();
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
           : (rw != w || rh != h)             ? "tamanho " + std::to_string(rw) + "x" + std::to_string(rh) + " em vez de " +
                                                    std::to_string(w) + "x" + std::to_string(h)
                                              : std::string("leitura do quadro");
    return false;
}

void Renderer::compare(std::span<const uint32_t> sw, uint32_t w, uint32_t h) {
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

void Renderer::dump(std::span<const uint32_t> sw, uint32_t w, uint32_t h, double frac) {
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

nr::DrawState draw_state(const PvrTri& t, uint32_t texture_handle) {
    return bs::native::render::kamui::draw_state(t, texture_handle);  // N8.7: as regras ficam no ListSink
}

PvrTri pvr_state(const nr::DrawState& s, const TextureKey* tex) {  // a mesma conversao do renderizador em software
    return soft::tri_state(s, tex ? tex->tcw : 0, tex ? tex->width : 0, tex ? tex->height : 0);
}

nr::Vertex vertex(const PvrVertex& v) { return bs::native::render::kamui::ir_vertex(v); }

PvrVertex pvr_vertex(const nr::Vertex& v) { return soft::tri_vertex(v); }

nr::FrameParams frame_params(uint32_t width, uint32_t height) { return pvr_frame_params(width, height); }

PvrFrame pvr_frame(const Frame& f) { return soft::tri_frame(f.cl.frame, f.texture_stride); }

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
    if (fmt == 6) {  // N8.7: paleta de 8 bits: o banco dos bits 26-25 do TCW; com mipmap, o maior nivel
        std::vector<uint32_t> pal(256);
        const uint32_t        bank = (k.tcw >> 25) & 3;
        for (uint32_t i = 0; i < 256; ++i) pal[i] = pvr_pal_entry(bank * 256 + i);
        const uint32_t off = mip ? pvr_mip_offset(6, false, w) : 0;
        const auto     d   = read(off + w * h);
        return nr::convert_pal8(std::span<const uint8_t>(d).subspan(off), w, h, pal, argb);
    }
    return false;  // YUV (o conversor saiu do produto no N8.5), bump e paleta de 4 bits com mipmap: sem conversao nativa
}

// N8.7 3b: o intervalo do caminho de 64 bits que a textura ocupa (o mesmo que texture_source le)
void texture_span(const TextureKey& k, uint32_t& base, uint32_t& n) {
    const uint32_t fmt = (k.tcw >> 27) & 7;
    const bool     mip = k.tcw >> 31, vq = (k.tcw >> 30) & 1, linear = (k.tcw >> 26) & 1, strideSel = (k.tcw >> 25) & 1;
    const uint32_t w = k.width, h = k.height;
    const uint32_t bits = fmt == 5 ? 4 : (fmt == 6 ? 8 : 16);
    base = (k.tcw & 0x1FFFFF) << 3;
    if (vq) n = 2048 + (mip ? pvr_mip_offset(fmt, true, w) : 0) + (w / 2) * (h / 2);
    else if (linear && strideSel && k.stride) n = 2 * k.stride * h;
    else if (mip) n = pvr_mip_offset(fmt, false, w) + bits * w * w / 8;
    else n = bits * w * h / 8;
}

uint64_t texture_generation(const TextureKey& k) {
    uint32_t base = 0, n = 0;
    texture_span(k, base, n);
    return vram_generation64(base, n);
}

bool texture_palette(const TextureKey& k) {
    const uint32_t fmt = (k.tcw >> 27) & 7;
    return fmt == 5 || fmt == 6;
}

void texture_source(const TextureKey& k, std::vector<uint8_t>& raw) {
    const uint32_t fmt  = (k.tcw >> 27) & 7;
    uint32_t       base = 0, n = 0;
    texture_span(k, base, n);  // N8.7 3b: o mesmo intervalo da geracao
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
#if !BS_VALIDACAO
    // N8.7: o produto nao decodifica texels da VRAM: sem conversao nativa, o render falha com o motivo (nunca um
    // desenho sem textura nem um recuo silencioso)
    return false;
#else
    // sem conversao nativa (paleta de 8 bits, YUV, bump): o decodificador do legado, o mesmo que o software amostra
    // (so chegam aqui formatos que ele aceita: os outros nem viram desenho)
    const TexInfo t{k.tcw, k.width, k.height, k.stride};
    argb.resize(size_t(k.width) * k.height);
    for (uint32_t y = 0; y < k.height; ++y)
        for (uint32_t x = 0; x < k.width; ++x) argb[size_t(y) * k.width + x] = pvr_texel(t, x, y);
    legacy = true;
    return true;
#endif
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
    return g_core ? g_core->hires_for(shown, width, height) : HiresImage{};
}

bool read_hires(const HiresImage& h, std::vector<uint32_t>& argb) {
    uint32_t w = 0;
    return g_core && g_core->read_hires(h, argb, w) && w == h.width;
}

namespace {
constexpr char kDumpMagic[8] = {'B', 'S', 'I', 'R', '2', 0, 0, 0};  // 2: N8.7 (ModifierVolume e DrawState novos)
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
    if (!g_core) g_core = std::make_unique<Renderer>();
    g_core->dumpDir = dir, g_core->dumpEvery = every ? every : 300;
}

// N8.7 5a: o produto entrega a IR do ListSink direto ao renderizador (system/render_bus); a validacao monta a IR pelos
// retornos do decodificador do legado (o adaptador de render_capture.cpp, o unico que conhece o PvrCaptureSink)
namespace {
void connect(Renderer* r) {
#if BS_VALIDACAO
    connect_legacy_decoder(r);
#else
    pvr_render_set_renderer(r);
#endif
}
} // namespace

void install(Mode m, uint32_t every) {
    if (m == Mode::Off) {  // desinstala e descarta (estatisticas, quadro, dispositivo Vulkan)
        connect(nullptr);
        g_core.reset();
        return;
    }
    auto next       = std::make_unique<Renderer>();  // estatisticas, quadro e conjuntos de ids recomecam
    next->dumpDir   = g_core ? g_core->dumpDir : std::string();
    next->dumpEvery = g_core ? g_core->dumpEvery : 300;
    next->mode      = m;
    next->every     = every ? every : 1;
    connect(next.get());  // o novo antes de o anterior sair
    g_core = std::move(next);
}

const Frame& last_frame() {
    static const Frame empty;
    return g_core ? g_core->frame : empty;
}

const Stats& stats() {
    static const Stats empty;
    return g_core ? g_core->st : empty;
}

std::string summary() {
    if (!g_core) return {};
    const Stats& s = g_core->st;
    char         b[1400];
    std::snprintf(b, sizeof b,
                  "[render-ir] renders %llu (conferidos %llu): desenhos %llu, vertices %llu, triangulos %llu, sprites %llu; "
                  "IR invalida %llu, triangulos divergentes %llu, imagens divergentes %llu; texturas conferidas %llu "
                  "(divergentes %llu, sem conversao nativa %llu; chaves distintas %llu, conteudos distintos %llu); quadros "
                  "divergentes %llu; volumes modificadores %llu (%llu triangulos, sem fechamento %llu), parametros "
                  "omitidos %llu; fog desenhos tabela/vertice/nenhum/tabela2 %llu/%llu/%llu/%llu (primeiro render "
                  "%llu/%llu/%llu/%llu), primeiro volume render %llu%s%s\n",
                  (unsigned long long)s.renders, (unsigned long long)s.checked, (unsigned long long)s.draws,
                  (unsigned long long)s.vertices, (unsigned long long)s.triangles, (unsigned long long)s.sprites,
                  (unsigned long long)s.invalid, (unsigned long long)s.triangle_mismatch, (unsigned long long)s.image_mismatch,
                  (unsigned long long)s.textures_checked, (unsigned long long)s.texture_mismatch,
                  (unsigned long long)s.textures_without_native, (unsigned long long)s.distinct_texture_keys,
                  (unsigned long long)s.distinct_texture_ids, (unsigned long long)s.frame_mismatch,
                  (unsigned long long)s.modifier_volumes, (unsigned long long)s.modifier_params,
                  (unsigned long long)s.modifier_unclosed, (unsigned long long)s.skipped_params,
                  (unsigned long long)s.fog_draws[0], (unsigned long long)s.fog_draws[1],
                  (unsigned long long)s.fog_draws[2], (unsigned long long)s.fog_draws[3],
                  (unsigned long long)s.fog_first_render[0], (unsigned long long)s.fog_first_render[1],
                  (unsigned long long)s.fog_first_render[2], (unsigned long long)s.fog_first_render[3],
                  (unsigned long long)s.modifier_first_render,
                  s.first_error.empty() ? "" : "; primeira: ", s.first_error.c_str());
    std::string out = b;
    std::snprintf(b, sizeof b,
                  "[render-ir] sombra: desenhos %llu (primeiro render %llu, FPU_SHAD_SCALE %03X); volumes sobre translucidos "
                  "%llu; inclusao %llu, exclusao %llu, com OU %llu, descarte misturado %llu, instrucao invalida %llu; dois volumes %llu; fog no primeiro render com tabela: FOG_DENSITY %04X, FOG_COL_RAM %06X, entradas "
                  "nao nulas %u de 128, maior valor %u\n",
                  (unsigned long long)s.shadow_draws, (unsigned long long)s.shadow_first_render, s.shadow_scale_first,
                  (unsigned long long)s.translucent_volumes, (unsigned long long)s.volume_inclusion,
                  (unsigned long long)s.volume_exclusion, (unsigned long long)s.volume_or, (unsigned long long)s.volume_mixed_cull,
                  (unsigned long long)s.volume_bad_instruction, (unsigned long long)s.two_volume, s.fog_density_first,
                  s.fog_color_first & 0xFFFFFF, s.fog_table_nonzero_first, s.fog_table_max_first);
    out += b;
    if (s.soft_frames || s.soft_failures || s.soft_image_mismatch) {
        std::snprintf(b, sizeof b, "[render-soft] quadros %llu, falhas %llu, imagens diferentes do caminho do TA %llu%s%s\n",
                      (unsigned long long)s.soft_frames, (unsigned long long)s.soft_failures,
                      (unsigned long long)s.soft_image_mismatch, s.soft_error.empty() ? "" : "; primeira: ", s.soft_error.c_str());
        out += b;
    }
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
    if (s.list_checked) {  // N8.7: o ListSink nativo contra a captura
        std::snprintf(b, sizeof b,
                      "[lista-nativa] renders conferidos %llu, IR diferentes %llu, fins de lista diferentes %llu; volumes "
                      "inclusao %llu (captura %llu), "
                      "exclusao %llu (%llu), com OU %llu (%llu)%s%s\n",
                      (unsigned long long)s.list_checked, (unsigned long long)s.list_mismatch,
                      (unsigned long long)ta_list_end_mismatches(),
                      (unsigned long long)s.list_volume_inclusion, (unsigned long long)s.volume_inclusion,
                      (unsigned long long)s.list_volume_exclusion, (unsigned long long)s.volume_exclusion,
                      (unsigned long long)s.list_volume_or, (unsigned long long)s.volume_or,
                      s.list_error.empty() ? "" : "; primeira: ", s.list_error.c_str());
        out += b;
    }
    out += pvr_frame_state_summary();  // N8.7 3a (validacao): o FrameState em sombra contra o banco do legado
    out += vram_heap_summary();        // N8.7 3b (validacao): a memoria das texturas em sombra contra o modelo da VRAM
    out += pvr_display_summary();      // N8.7 3c (validacao): o servico de exibicao em sombra contra a saida do legado
    out += holly_event_status_summary();  // N8.7 4a (validacao): o banco de status em sombra contra o controlador
    out += sh4_dmac_ch2_summary();        // N8.7 4b (validacao): a copia do canal 2 em sombra contra o DMAC
    out += ta_area4_summary();            // N8.7 4c (validacao): a entrada da area 4 em sombra contra o TA
    out += pvr_render_pass_summary();     // N8.7 4d (validacao): a passagem de render em sombra contra a referencia
#if BS_VALIDACAO
    out += "[texture-cache] texturas velhas pela geracao " + std::to_string(s.tex_stale) +
           (s.tex_stale_error.empty() ? std::string() : "; primeira: " + s.tex_stale_error) + "\n";
#endif
    if (s.hires_renders) {  // N7c: resolucao interna
        char r[300];
        std::snprintf(r, sizeof r,
                      "[render-escala] alvo %ux%u: renders %llu; VBlanks com a imagem maior %llu, sem %llu; leitura de volta "
                      "(so a reduzida, feita na GPU): media %.2f ms por render\n",
                      s.hires_w, s.hires_h, (unsigned long long)s.hires_renders, (unsigned long long)s.hires_shown,
                      (unsigned long long)s.hires_missed, s.hires_cpu_ms_total / double(s.hires_renders));
        out += r;
    }
    return out;
}

} // namespace bs::transitional::render_capture
