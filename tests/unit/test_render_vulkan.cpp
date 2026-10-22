// N7b: o backend Vulkan (native/render/vulkan) nas cenas de fidelidade (native/render/fidelity_scenes): cada prova
// das cenas fieis tem de sair na tolerancia da cena. Sem GPU/Vulkan na maquina: [skip] (com BS_EXIGIR_DADOS=1,
// falha). Tambem: determinismo (o mesmo quadro duas vezes, a mesma imagem) e 0 objetos vivos no fim.
#include "test.h"

#include "render/fidelity_scenes.h"
#include "render/vulkan/vk_device.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace nr = bs::native::render;

namespace {
bool close(uint32_t a, uint32_t b, int tol) {
    for (int sh = 0; sh <= 16; sh += 8)
        if (std::abs(int((a >> sh) & 0xFF) - int((b >> sh) & 0xFF)) > tol) return false;
    return true;
}

// renderiza a cena no dispositivo; false se algo falhar
bool run(nr::VulkanDevice& d, const nr::FidelityScene& s, std::vector<uint32_t>& img) {
    uint32_t w = 0, h = 0;
    if (s.image) return d.present_image(s.image_argb, s.cl.frame.width, s.cl.frame.height).ok() && d.read_frame(img, w, h).ok();
    std::vector<nr::TextureHandle> handles;
    for (const auto& t : s.textures) {
        auto r = d.create_texture(t.desc, std::as_bytes(std::span<const uint32_t>(t.argb)));
        if (!r) return false;
        handles.push_back(r.value());
    }
    nr::CommandList cl = s.cl;
    for (size_t i = 0; i < cl.draws.size(); ++i)
        cl.draws[i].state.texture = s.draw_texture[i] >= 0 ? handles[size_t(s.draw_texture[i])] : nr::TextureHandle{};
    const bool ok = d.submit(cl).ok() && d.read_frame(img, w, h).ok() && w == cl.frame.width && h == cl.frame.height;
    for (auto h : handles) d.destroy_texture(h);
    return ok;
}
} // namespace

BS_TEST(render_vulkan_fidelity_scenes) {
    std::string err;
    {
        auto d = nr::VulkanDevice::create(&err);
        if (!d) {
            std::fprintf(stderr, "  (sem Vulkan: %s)\n", err.c_str());
            BS_SKIP("dispositivo Vulkan");
        }
        std::fprintf(stderr, "  dispositivo: %s\n", d->stats().device.c_str());
        int scenes = 0, eligible = 0, probes = 0;
        for (const auto& s : nr::fidelity_scenes()) {
            if (!s.blocked.empty() || s.mode != nr::SceneMode::Faithful) continue;
            ++eligible;
            std::vector<uint32_t> img;
            const bool            ok = run(*d, s, img);
            BS_CHECK(ok);
            if (!ok) continue;
            ++scenes;
            for (const auto& p : s.probes) {
                ++probes;
                const uint32_t got  = img[size_t(p.y) * s.cl.frame.width + p.x] & 0xFFFFFF;
                const bool     good = close(got, p.rgb, p.tolerance);
                if (!good) std::fprintf(stderr, "  cena %s (%u,%u): Vulkan %06X, esperado %06X (tol %u)\n", s.name.c_str(), p.x, p.y, got, p.rgb, p.tolerance);
                BS_CHECK(good);
            }
            // determinismo: o mesmo quadro de novo
            std::vector<uint32_t> again;
            BS_CHECK(run(*d, s, again) && again == img);
        }
        std::fprintf(stderr, "  cenas fieis %d de %d, provas %d\n", scenes, eligible, probes);
        BS_CHECK(scenes == eligible && eligible >= 7);
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}
