#pragma once
// N8.7 5a (docs/N8-7-KAMUI.md): o renderizador sobre a IR (render_core.cpp), interno a render_capture e nos dois builds.
// Desenha o quadro (Frame) no dispositivo do modo (Vulkan, com o software sobre a IR como recuo, ou so o software), com
// o cache das texturas, a resolucao interna e as estatisticas. No produto ele recebe a IR do ListSink (PvrListRenderer);
// na validacao o adaptador do decodificador do legado (render_capture.cpp) monta `frame` e chama draw().
#include "render_capture.h"
#include "render/soft/soft_device.h"
#include "render/vulkan/vk_device.h"

#include <deque>
#include <map>
#include <memory>
#include <tuple>

namespace bs::transitional::render_capture {

namespace soft = bs::native::render::soft;

class Renderer final : public PvrListRenderer {
public:
    Mode        mode  = Mode::Off;
    uint32_t    every = 1;
    Frame       frame;
    Stats       st;
    std::string dumpDir;
    uint32_t    dumpEvery = 300;

    ~Renderer() override;

    // desenha `frame` no dispositivo do modo e preenche `image` (w x h ARGB8888); false = nada desenhado
    bool draw(uint32_t w, uint32_t h, std::vector<uint32_t>& image);
    // N8.7 (produto): a IR do ListSink no lugar da montagem pelos retornos do decodificador do legado
    bool render_list(const bs::native::render::kamui::ListFrame& lf, uint32_t w, uint32_t h,
                     std::vector<uint32_t>& image) override;
    void account();  // as estatisticas de cada render (desenhos, fog, sombra, volumes)
    bool soft_render(uint32_t w, uint32_t h, std::vector<uint32_t>& out);
    void compare(std::span<const uint32_t> sw, uint32_t w, uint32_t h);  // Mode::Compare: o Vulkan contra o software
    bool vk_ok() const { return vkOk_; }                                   // o ultimo draw() saiu do Vulkan
    HiresImage hires_for(std::span<const uint32_t> shown, uint32_t w, uint32_t h);
    bool       read_hires(const HiresImage& h, std::vector<uint32_t>& argb, uint32_t& w);

private:
    struct VkTex {
        nr::TextureHandle    handle;
        std::vector<uint8_t> raw;
        uint64_t             used = 0;
        uint32_t             w = 0, h = 0;  // o tamanho no dispositivo (o da substituicao, se houver)
        const void*          provider = nullptr;
        uint64_t             gen = 0, pal = 0;  // N8.7 3b: as geracoes da memoria e da paleta quando os bytes foram lidos
    };
    using TexCache = std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, VkTex>;

    bool ensure_vk();
    bool texture_handle(nr::IRenderDevice& dev, TexCache& cache, bool hd, const TextureKey& k, nr::TextureHandle& out);
    void evict(nr::IRenderDevice& dev, TexCache& cache);
    bool soft_fail(const std::string& why);
    static constexpr uint32_t kRecoveries = 3;
    bool vk_render(uint32_t w, uint32_t h, std::vector<uint32_t>& out);
    void drop_vk(bool lost);
    bool vk_attempt(uint32_t w, uint32_t h, std::vector<uint32_t>& out, std::string* why);
    void dump(std::span<const uint32_t> sw, uint32_t w, uint32_t h, double frac);

    // ---- N7b: backend Vulkan ----
    std::shared_ptr<nr::VulkanDevice> vk_;  // o dispositivo do processo (o mesmo da apresentacao direta)
    bool                              vkTried_ = false, vkOk_ = false;
    std::vector<uint32_t>             vkImage_;
    TexCache                          vkTex_;
    std::vector<uint8_t>              raw_;
    std::vector<uint32_t>             argb_;
    // N8.7: o renderizador em software sobre a IR
    std::unique_ptr<soft::SoftwareDevice> soft_;
    TexCache                              softTex_;

    // N7c: os ultimos renders na escala (o mais novo na frente): a imagem reduzida que foi para a VRAM (para achar o
    // render exibido) e a maior. Tres por causa do buffer duplo (o exibido e o anterior ao ultimo) com folga.
    struct Hires {
        std::vector<uint32_t> down;
        uint64_t              seq = 0;
        uint32_t              w = 0, h = 0, iw = 0, ih = 0;
    };
    std::deque<Hires> hires_;
    uint32_t          dumped_ = 0;
};

#if BS_VALIDACAO
// render_capture.cpp: liga o adaptador do decodificador do legado ao renderizador (nullptr desliga)
void connect_legacy_decoder(Renderer* r);
#endif

} // namespace bs::transitional::render_capture
