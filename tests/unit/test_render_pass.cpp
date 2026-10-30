// N8.7 (docs/N8-7-KAMUI.md, incremento 4d): a passagem de render nativa (native/render/kamui/render_pass): tamanho,
// fundo pela memoria de parametros, Region Array tipo 2 e a gravacao no framebuffer (formato, clip, faixas marcadas).
#include "test.h"

#include "render/kamui/render_pass.h"

#include <cstring>
#include <map>
#include <utility>
#include <vector>

using bs::native::render::kamui::RenderPass;

namespace {

struct Pvr {
    std::map<uint32_t, uint32_t>               regs;
    std::vector<uint8_t>                       mem = std::vector<uint8_t>(RenderPass::kMemSize);
    std::vector<std::pair<uint32_t, uint32_t>> touched;
    void     set(uint32_t off, uint32_t v) { regs[0x005F8000 + off] = v; }
    void     put(uint32_t a, uint32_t v) { std::memcpy(mem.data() + a, &v, 4); }
    uint16_t px16(uint32_t a) const {
        uint16_t v;
        std::memcpy(&v, mem.data() + a, 2);
        return v;
    }
};

uint32_t reg(void* ctx, uint32_t p) {
    auto& r = static_cast<Pvr*>(ctx)->regs;
    auto  i = r.find(p);
    return i == r.end() ? 0 : i->second;
}
void touch(void* ctx, uint32_t off, uint32_t n) { static_cast<Pvr*>(ctx)->touched.emplace_back(off, n); }

} // namespace

// TA_GLOB_TILE_CLIP: (colunas + 1) e (linhas + 1) tiles de 32 pixels
BS_TEST(render_pass_size_from_the_tile_clip) {
    Pvr p;
    p.set(0x13C, (14u << 16) | 19);
    const RenderPass::Size s = RenderPass::size(reg, &p);
    BS_CHECK(s.width == 640 && s.height == 480);
}

// o fundo: cor do primeiro vertice em (PARAM_BASE & 0xF00000) + tag * 4, pulando x, y, z (e u, v se textura)
BS_TEST(render_pass_background_from_parameter_memory) {
    Pvr p;
    p.set(0x020, 0x00123456);           // PARAM_BASE: so os bits 20-23 contam
    p.set(0x08C, (1u << 24) | (8 << 3));  // ISP_BACKGND_T: skip 1, tag 8 (32 bytes), primeiro vertice
    const uint32_t base = 0x100000 + 32;
    p.put(base, 0);                       // ISP sem textura
    p.put(base + 12 + 12, 0x00ABCDEF);    // x, y, z e a cor
    RenderPass::Background b = RenderPass::background(reg, &p, p.mem.data());
    BS_CHECK(b.color == 0x00ABCDEF && !b.textured);
    p.put(base, (1u << 25));              // com textura, UV de 32 bits: pula 8 bytes
    p.put(base + 12 + 12 + 8, 0x00112233);
    b = RenderPass::background(reg, &p, p.mem.data());
    BS_CHECK(b.color == 0x00112233 && b.textured);
}

// Region Array tipo 2: um tile pre-sort com o global em autosort e avisado; a ultima entrada (bit 31) encerra
BS_TEST(render_pass_region_array_sort_mode) {
    Pvr p;
    p.set(0x02C, 0x200000);
    p.put(0x200000, 0);                    // autosort
    p.put(0x200000 + 24, 1u << 31);        // ultima, autosort
    BS_CHECK(!RenderPass::region_sort_mismatch(reg, &p, p.mem.data(), true));  // tipo 1: nao se olha
    p.set(0x07C, 1u << 21);
    BS_CHECK(!RenderPass::region_sort_mismatch(reg, &p, p.mem.data(), true));
    p.put(0x200000 + 24, (1u << 31) | (1u << 29));  // a ultima pede pre-sort
    BS_CHECK(RenderPass::region_sort_mismatch(reg, &p, p.mem.data(), true));
    BS_CHECK(RenderPass::region_sort_mismatch(reg, &p, p.mem.data(), false));  // global pre-sort: a primeira ja diverge
}

// 565 no framebuffer com stride e clip; cada linha gravada e uma faixa marcada
BS_TEST(render_pass_write_back_565_with_clip) {
    Pvr p;
    p.set(0x048, 1);                   // 565
    p.set(0x060, 0x100000);            // FB_W_SOF1
    p.set(0x04C, 64 / 8);              // 64 bytes por linha
    p.set(0x068, (2u << 16) | 1);      // colunas 1-2
    p.set(0x06C, (1u << 16) | 0);      // linhas 0-1
    std::vector<uint32_t> img(4 * 3, 0xFFFF0000);
    img[1]  = 0xFF00FF00;
    RenderPass::write_back(reg, &p, img, 4, 3, p.mem.data(), touch);
    BS_CHECK(p.px16(0x100000 + 2) == 0x07E0 && p.px16(0x100000 + 4) == 0xF800);
    BS_CHECK(p.px16(0x100000) == 0 && p.px16(0x100000 + 6) == 0);  // fora do clip
    BS_CHECK(p.px16(0x100000 + 128 + 2) == 0);                       // linha 2 fora do clip
    BS_CHECK(p.touched.size() == 2 && p.touched[0] == std::make_pair(0x100002u, 4u) &&
             p.touched[1] == std::make_pair(0x100042u, 4u));
}

// 1555 pelo limiar de alfa (FB_W_CTRL 23-16) e 0888 com o byte alto K (15-8)
BS_TEST(render_pass_write_back_alpha_threshold_and_k) {
    Pvr p;
    p.set(0x060, 0x0);
    p.set(0x04C, 8 / 8);
    p.set(0x068, 1u << 16), p.set(0x06C, 0);
    std::vector<uint32_t> img = {0x80FFFFFF, 0x7FFFFFFF};
    p.set(0x048, 3 | (0x80u << 16));
    RenderPass::write_back(reg, &p, img, 2, 1, p.mem.data(), touch);
    BS_CHECK(p.px16(0) == 0xFFFF && p.px16(2) == 0x7FFF);
    p.set(0x048, 5 | (0x5Au << 8));
    RenderPass::write_back(reg, &p, img, 2, 1, p.mem.data(), touch);
    uint32_t v;
    std::memcpy(&v, p.mem.data(), 4);
    BS_CHECK(v == 0x5AFFFFFF);
}
