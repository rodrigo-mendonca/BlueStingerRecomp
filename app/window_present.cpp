#include "window_present.h"

#include "render/vulkan/vk_device.h"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

namespace bs {

namespace nr = native::render;

SDL_FRect game_area(int w, int h) {
    if (w <= 0 || h <= 0) return {0, 0, 640, 480};
    const float sw = float(w), sh = float(h);
    const float aw = (std::min)(sw, sh * 4.0f / 3.0f), ah = aw * 3.0f / 4.0f;
    return {std::floor((sw - aw) / 2), std::floor((sh - ah) / 2), std::floor(aw), std::floor(ah)};
}

namespace {

// ---- SDL (recuo) ----
class SdlPresenter final : public WindowPresenter {
public:
    SdlPresenter(SDL_Window* w, SDL_Renderer* r) : window_(w), r_(r) {}
    ~SdlPresenter() override {
        for (SDL_Texture* t : {game_, layer_[0], layer_[1]})
            if (t) SDL_DestroyTexture(t);
    }
    const char* name() const override { return "SDL"; }
    void        output_size(int& w, int& h) override {
        w = h = 0;
        SDL_GetCurrentRenderOutputSize(r_, &w, &h);
    }
    void set_vsync(bool on) override { SDL_SetRenderVSync(r_, on ? 1 : SDL_RENDERER_VSYNC_DISABLED); }

    bool present(const GameFrame& f, uint64_t frameSeq, const SDL_FRect& area, std::span<const WindowLayer> layers,
                 std::vector<uint32_t>* capture) override {
        bool ok = true;
        if (f.width && f.height && frameSeq != seq_) {
            if (!game_ || gw_ != f.width || gh_ != f.height) {
                if (game_) SDL_DestroyTexture(game_);
                game_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, int(f.width), int(f.height));
                gw_ = f.width, gh_ = f.height;
                if (!game_) return false;
            }
            ok &= SDL_UpdateTexture(game_, nullptr, f.argb.data(), int(f.width * 4));
            seq_ = frameSeq;
        }
        SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
        ok &= SDL_RenderClear(r_);
        if (game_) ok &= SDL_RenderTexture(r_, game_, nullptr, &area);
        for (size_t i = 0; i < layers.size() && i < 2; ++i) {
            const WindowLayer& l = layers[i];
            if (!l.argb || l.argb->size() < size_t(l.width) * l.height) continue;
            if (!layer_[i] || lw_[i] != l.width || lh_[i] != l.height) {
                if (layer_[i]) SDL_DestroyTexture(layer_[i]);
                layer_[i] = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, int(l.width), int(l.height));
                if (layer_[i]) SDL_SetTextureBlendMode(layer_[i], SDL_BLENDMODE_BLEND_PREMULTIPLIED);
                lw_[i] = l.width, lh_[i] = l.height, lv_[i] = ~0ull;
            }
            if (!layer_[i]) continue;
            if (lv_[i] != l.version) SDL_UpdateTexture(layer_[i], nullptr, l.argb->data(), int(l.width * 4)), lv_[i] = l.version;
            SDL_RenderTexture(r_, layer_[i], nullptr, nullptr);
        }
        if (capture) {
            capture->clear();
            if (SDL_Surface* s = SDL_RenderReadPixels(r_, nullptr)) {
                if (SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888)) {
                    capture->resize(size_t(c->w) * c->h);
                    for (int y = 0; y < c->h; ++y)
                        std::memcpy(capture->data() + size_t(y) * c->w, static_cast<uint8_t*>(c->pixels) + size_t(y) * c->pitch,
                                    size_t(c->w) * 4);
                    SDL_DestroySurface(c);
                }
                SDL_DestroySurface(s);
            }
        }
        ok &= SDL_RenderPresent(r_);
        return ok;
    }

private:
    SDL_Window*   window_;
    SDL_Renderer* r_;
    SDL_Texture*  game_ = nullptr;
    SDL_Texture*  layer_[2] = {};
    uint32_t      gw_ = 0, gh_ = 0, lw_[2] = {}, lh_[2] = {};
    uint64_t      lv_[2] = {~0ull, ~0ull}, seq_ = ~0ull;
};

// ---- Vulkan (direta) ----
class VulkanPresenter final : public WindowPresenter {
public:
    explicit VulkanPresenter(SDL_Window* w) : window_(w) {}
    ~VulkanPresenter() override { release(); }

    bool init(std::string* error) {
        dev_ = nr::vulkan_shared(error);
        if (!dev_) return false;
        generation_ = nr::vulkan_shared_generation();
        instance_   = reinterpret_cast<VkInstance>(dev_->instance_handle());
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
            if (error) *error = std::string("superficie: ") + SDL_GetError();
            surface_ = {};
            return false;
        }
        if (!dev_->present_attach(reinterpret_cast<uint64_t>(surface_), error)) return false;
        loaded_.clear();  // os shaders sao do dispositivo: num dispositivo novo, carregados de novo
        if (std::string e = build(); !e.empty()) postError_ = e;  // o menu o desativa (take_post_error)
        std::fprintf(stderr, "[apresentacao] Vulkan direta: %s\n", dev_->stats().device.c_str());
        return true;
    }

    const char* name() const override { return "Vulkan"; }
    void        output_size(int& w, int& h) override {
        w = h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
    }
    void set_vsync(bool on) override { vsync_ = on; }

    bool present(const GameFrame& f, uint64_t frameSeq, const SDL_FRect& area, std::span<const WindowLayer> layers,
                 std::vector<uint32_t>* capture) override {
        // o renderizador perdeu o dispositivo e criou outro: a apresentacao vai junto (outra superficie)
        if (nr::vulkan_shared_generation() != generation_ && !reattach()) return false;
        if (!dev_) return false;
        int w = 0, h = 0;
        output_size(w, h);
        nr::PresentRequest r;
        r.out_w = uint32_t(std::max(w, 0)), r.out_h = uint32_t(std::max(h, 0)), r.vsync = vsync_;
        r.area[0] = area.x, r.area[1] = area.y, r.area[2] = area.w, r.area[3] = area.h;
        r.slot_seq = f.hiresSeq, r.slot_y0 = f.hiresY0, r.slot_rows = f.hiresRows;
        if (f.width && f.height) r.argb = f.argb.data(), r.width = f.width, r.height = f.height, r.argb_version = frameSeq;
        r.post = passes_.data(), r.post_count = uint32_t(passes_.size());
        // indice fixo por camada (0 legendas, 1 menu): a versao de uma nunca e confundida com a da outra
        r.layer_count = 2;
        for (size_t i = 0; i < layers.size() && i < 2; ++i) {
            const WindowLayer& l = layers[i];
            if (l.argb && l.argb->size() >= size_t(l.width) * l.height) r.layers[i] = {l.argb->data(), l.width, l.height, l.version};
        }
        r.capture = capture;
        const auto res = dev_->present(r);
        if (!res && res.error() == native::Error::Io) {  // dispositivo perdido: o compartilhado e descartado e refeito
            // com um shader de mod ativo, ele e o primeiro suspeito (um passe que prende a GPU): sai antes de refazer
            if (spec_.shader) postError_ = "perda do dispositivo com o shader ativo", spec_.shader = nullptr, spec_.shaderParams.clear();
            nr::vulkan_shared_lost(dev_.get());
            reattach();
        }
        return bool(res);
    }

    std::string set_post(const PostSpec& s) override {
        spec_ = s;
        return build();
    }
    std::string take_post_error() override { return std::exchange(postError_, std::string()); }

    std::string stats() const override {
        if (!dev_) return {};
        const nr::PresentStats& s = dev_->present_stats();
        char                    b[400];
        std::snprintf(b, sizeof b,
                      "[apresentacao] Vulkan: %llu quadros (da GPU, resolucao interna: %llu; imagem da CPU: %llu), swapchain criada "
                      "%llu vez(es); trabalho da CPU por quadro: media %.2f ms, pior %.1f ms (esperas, V-Sync incluso: media %.2f ms); "
                      "enviado %.1f MB; com pos-processamento: %llu\n",
                      (unsigned long long)s.frames, (unsigned long long)s.slot_frames, (unsigned long long)s.cpu_frames,
                      (unsigned long long)s.recreated, s.frames ? s.cpu_ms_total / double(s.frames) : 0.0, s.cpu_ms_max,
                      s.frames ? s.wait_ms_total / double(s.frames) : 0.0, double(s.upload_bytes) / 1048576.0,
                      (unsigned long long)s.post_frames);
        return b;
    }

private:
    SDL_Window*                       window_;
    std::shared_ptr<nr::VulkanDevice> dev_;
    VkInstance                        instance_ = {};
    VkSurfaceKHR                      surface_  = {};
    uint64_t                          generation_ = 0;
    bool                              vsync_ = true;
    PostSpec                          spec_;
    std::vector<nr::PostPass>         passes_;
    std::map<std::string, int>        loaded_;  // shader de mod -> id no dispositivo atual
    std::string                       postError_;

    // os passes do pedido: o shader de mod "antes" dos internos, a correcao de cor, o shader de mod "depois"
    std::string build() {
        passes_.clear();
        if (!dev_) return {};
        std::string err;
        int         mod = -1;
        if (spec_.shader) {
            auto it = loaded_.find(spec_.shader->name);
            if (it == loaded_.end()) it = loaded_.emplace(spec_.shader->name, dev_->post_load(spec_.shader->spirv, &err)).first;
            mod = it->second;
            if (mod < 0) {
                if (err.empty()) err = "o dispositivo recusou o shader";
                std::fprintf(stderr, "[pos] shader %s desativado: %s\n", spec_.shader->name.c_str(), err.c_str());
            }
        }
        nr::PostPass m;
        m.shader = mod;
        for (size_t i = 0; i < spec_.shaderParams.size() && i < nr::kPostParams; ++i) m.params[i] = spec_.shaderParams[i];
        if (mod >= 0 && spec_.shader->before_builtin) passes_.push_back(m);
        // ordem dos internos: bloom (sobre a imagem do jogo), tonemapping, correcao de cor, nitidez
        if (spec_.bloom) {
            nr::PostPass h, v;
            h.shader = dev_->post_builtin(nr::PostBuiltin::BloomBlurH), v.shader = dev_->post_builtin(nr::PostBuiltin::BloomBlurV);
            h.params[0] = spec_.bloomThreshold, h.params[1] = v.params[1] = spec_.bloomRadius, v.params[2] = spec_.bloomStrength;
            v.original = int(passes_.size());  // o binding 1 do passe vertical: a imagem de antes do bloom
            if (h.shader >= 0 && v.shader >= 0) passes_.push_back(h), passes_.push_back(v);
        }
        if (spec_.tonemap) {
            nr::PostPass t;
            t.shader    = dev_->post_builtin(nr::PostBuiltin::Tonemap);
            t.params[0] = spec_.exposure, t.params[1] = spec_.highlights, t.params[2] = spec_.toneShadows;
            if (t.shader >= 0) passes_.push_back(t);
        }
        if (spec_.color) {
            nr::PostPass c;
            c.shader = dev_->post_builtin_color();
            c.params[0] = spec_.brightness, c.params[1] = spec_.contrast, c.params[2] = spec_.gamma, c.params[3] = spec_.saturation;
            if (c.shader >= 0) passes_.push_back(c);
        }
        if (spec_.sharpen > 0) {  // por ultimo entre os de cor: realca a imagem ja corrigida
            nr::PostPass n;
            n.shader = dev_->post_builtin(nr::PostBuiltin::Sharpen), n.params[0] = spec_.sharpen;
            if (n.shader >= 0) passes_.push_back(n);
        }
        if (mod >= 0 && !spec_.shader->before_builtin) passes_.push_back(m);
        return mod < 0 && spec_.shader ? err : std::string();
    }

    void release() {
        if (dev_) dev_->present_detach();
        if (surface_) SDL_Vulkan_DestroySurface(instance_, surface_, nullptr), surface_ = {};
        dev_.reset();
    }
    bool reattach() {
        release();
        std::string err;
        if (init(&err)) return true;
        std::fprintf(stderr, "[apresentacao] Vulkan: %s\n", err.c_str());
        release();
        return false;
    }
};

} // namespace

std::unique_ptr<WindowPresenter> make_sdl_presenter(SDL_Window* window, SDL_Renderer* renderer) {
    return renderer ? std::make_unique<SdlPresenter>(window, renderer) : nullptr;
}

std::unique_ptr<WindowPresenter> make_vulkan_presenter(SDL_Window* window, std::string* error) {
    auto p = std::make_unique<VulkanPresenter>(window);
    if (!p->init(error)) return nullptr;
    return p;
}

} // namespace bs
