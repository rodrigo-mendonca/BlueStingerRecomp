#include "render/kamui/render_pass.h"

#include <algorithm>
#include <cstring>

namespace bs::native::render::kamui {
namespace {

constexpr uint32_t R(uint32_t off) { return 0x005F8000 + off; }

float f32(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

uint32_t rd(const uint8_t* mem, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, mem + (a & (RenderPass::kMemSize - 1)), 4);
    return v;
}

} // namespace

RenderPass::Size RenderPass::size(Reg reg, void* ctx) {
    const uint32_t clip = reg(ctx, R(0x13C));  // TA_GLOB_TILE_CLIP
    return {((clip & 0x3F) + 1) * 32, (((clip >> 16) & 0xF) + 1) * 32};
}

RenderPass::Background RenderPass::background(Reg reg, void* ctx, const uint8_t* mem) {
    const uint32_t t    = reg(ctx, R(0x08C));  // ISP_BACKGND_T
    const uint32_t base = ((reg(ctx, R(0x020)) & 0xF00000) + ((t >> 3) & 0x1FFFFF) * 4) & (kMemSize - 1);
    const uint32_t isp  = rd(mem, base);
    const uint32_t skip = (t >> 24) & 7, vsize = (3 + skip) * 4;
    uint32_t       p    = base + 12 + (t & 7) * vsize + 12;  // pula x, y, z do 1o vertice
    Background     b;
    if (isp & (1u << 25)) {
        b.textured = true;
        p += (isp & (1u << 22)) ? 4 : 8;
    }
    b.color = rd(mem, p);
    return b;
}

FrameParams RenderPass::frame_params(Reg reg, void* ctx, uint32_t width, uint32_t height, uint32_t background_rgb) {
    FrameParams f;
    f.width = width, f.height = height;
    f.clear_color = 0xFF000000 | background_rgb;
    f.clear_depth = f32(reg(ctx, R(0x088)));  // ISP_BACKGND_D
    for (size_t i = 0; i < f.fog_table.size(); ++i) f.fog_table[i] = uint16_t(reg(ctx, R(0x200 + uint32_t(i) * 4)) & 0xFFFF);
    f.fog_color            = reg(ctx, R(0x0B0));           // FOG_COL_RAM
    f.fog_vertex_color     = reg(ctx, R(0x0B4));           // FOG_COL_VERT
    f.fog_density          = reg(ctx, R(0x0B8)) & 0xFFFF;  // FOG_DENSITY
    f.shadow_scale         = reg(ctx, R(0x074)) & 0x1FF;   // FPU_SHAD_SCALE
    f.punch_through_ref    = float(reg(ctx, R(0x11C)) & 0xFF) / 255.f;
    f.translucent_autosort = !(reg(ctx, R(0x098)) & 1);    // ISP_FEED_CFG bit 0
    const uint32_t half    = reg(ctx, R(0x080));           // HALF_OFFSET
    f.sample_center = half & 1, f.attribute_center = half & 2, f.texel_center = half & 4;
    f.small_cull = f32(reg(ctx, R(0x078)));  // FPU_CULL_VAL
    f.clamp_max = reg(ctx, R(0x0BC)), f.clamp_min = reg(ctx, R(0x0C0));
    return f;
}

bool RenderPass::region_sort_mismatch(Reg reg, void* ctx, const uint8_t* mem, bool autosort) {
    if (!((reg(ctx, R(0x07C)) >> 21) & 1)) return false;  // FPU_PARAM_CFG: Region Array tipo 1
    uint32_t a = reg(ctx, R(0x02C)) & 0x7FFFFC;            // REGION_BASE
    for (int n = 0; n < 4096; ++n, a += 24) {             // entradas de 6 palavras, a ultima com o bit 31
        const uint32_t ctrl    = rd(mem, a);
        const bool     presort = (ctrl >> 29) & 1;
        if (presort == autosort) return true;
        if (ctrl >> 31) return false;
    }
    return false;
}

void RenderPass::write_back(Reg reg, void* ctx, const std::vector<uint32_t>& color, uint32_t w, uint32_t h,
                            uint8_t* mem, Touch touch) {
    const uint32_t ctrl   = reg(ctx, R(0x048));               // FB_W_CTRL
    const uint32_t sof    = reg(ctx, R(0x060)) & 0x00FFFFFC;  // FB_W_SOF1
    const uint32_t stride = (reg(ctx, R(0x04C)) & 0x1FF) * 8;  // FB_W_LINESTRIDE
    const uint32_t xc = reg(ctx, R(0x068)), yc = reg(ctx, R(0x06C));
    const int      xmin = int(xc & 0x7FF), xmax = int((xc >> 16) & 0x7FF);
    const int      ymin = int(yc & 0x3FF), ymax = int((yc >> 16) & 0x3FF);
    // FB_W_CTRL: bits 2-0 formato, 3 dither, 15-8 K (bit de alfa do 0555 / byte alto do 0888), 23-16 limiar de alfa do
    // 1555
    const uint32_t   fmt = ctrl & 7, kval = (ctrl >> 8) & 0xFF, thr = (ctrl >> 16) & 0xFF;
    static const int bpp[8] = {2, 2, 2, 2, 3, 4, 4, 4};
    const int        wi = int(w), hi = int(h);
    const int        x0 = std::max(0, xmin), x1 = std::min(wi - 1, xmax);
    for (int y = std::max(0, ymin); y <= std::min(hi - 1, ymax); ++y) {
        for (int x = x0; x <= x1; ++x) {
            const uint32_t c = color[size_t(y) * wi + x];
            const uint32_t a = c >> 24, rr = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            uint32_t       v = 0;
            switch (fmt) {
            case 0: v = ((kval >> 7) << 15) | ((rr >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); break;  // 0555
            case 1: v = ((rr >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); break;                       // 565
            case 2: v = ((a >> 4) << 12) | ((rr >> 4) << 8) | ((g >> 4) << 4) | (b >> 4); break;     // 4444
            case 3: v = ((a >= thr) << 15) | ((rr >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); break;   // 1555
            case 4: v = c & 0xFFFFFF; break;                                                          // 888
            case 5: v = (kval << 24) | (c & 0xFFFFFF); break;                                         // 0888
            default: v = c; break;                                                                    // 8888
            }
            const uint32_t addr = (sof + uint32_t(y) * stride + uint32_t(x) * bpp[fmt]) & (kMemSize - 1);
            if (addr + uint32_t(bpp[fmt]) <= kMemSize) {
                std::memcpy(mem + addr, &v, size_t(bpp[fmt]));
            } else {  // o pixel da a volta no fim dos 8 MB (os leitores mascaram cada byte)
                for (int i = 0; i < bpp[fmt]; ++i) mem[(addr + uint32_t(i)) & (kMemSize - 1)] = uint8_t(v >> (8 * i));
            }
        }
        if (x1 >= x0)
            touch(ctx, (sof + uint32_t(y) * stride + uint32_t(x0) * bpp[fmt]) & (kMemSize - 1),
                  uint32_t(x1 - x0 + 1) * uint32_t(bpp[fmt]));
    }
}

} // namespace bs::native::render::kamui
