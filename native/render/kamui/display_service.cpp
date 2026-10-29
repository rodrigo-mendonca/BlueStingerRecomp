#include "render/kamui/display_service.h"

#include "render/kamui/texture_heap.h"

namespace bs::native::render::kamui {
namespace {

uint32_t expand5(uint32_t v) { return (v << 3) | (v >> 2); }
uint32_t expand6(uint32_t v) { return (v << 2) | (v >> 4); }

uint32_t pixel(const uint8_t* mem, uint32_t addr, uint32_t fmt) {
    auto at = [&](uint32_t a) { return mem[a & (TextureHeap::kSize - 1)]; };
    switch (fmt) {
    case 0: {  // 0555
        const uint32_t v = at(addr) | (at(addr + 1) << 8);
        return 0xFF000000 | (expand5((v >> 10) & 31) << 16) | (expand5((v >> 5) & 31) << 8) | expand5(v & 31);
    }
    case 1: {  // 565
        const uint32_t v = at(addr) | (at(addr + 1) << 8);
        return 0xFF000000 | (expand5(v >> 11) << 16) | (expand6((v >> 5) & 63) << 8) | expand5(v & 31);
    }
    default:  // 888 empacotado e 0888: B, G, R nos bytes baixos
        return 0xFF000000 | (uint32_t(at(addr + 2)) << 16) | (uint32_t(at(addr + 1)) << 8) | at(addr);
    }
}

} // namespace

void scanout(const DisplayRegs& r, const uint8_t* mem, std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height) {
    const uint32_t fmt = (r.fb_r_ctrl >> 2) & 3, bpp = fmt == 2 ? 3 : (fmt == 3 ? 4 : 2);
    const uint32_t words = (r.fb_r_size & 0x3FF) + 1, lines = ((r.fb_r_size >> 10) & 0x3FF) + 1;
    const uint32_t mod       = (r.fb_r_size >> 20) & 0x3FF;
    const uint32_t stride    = (words + mod - (mod ? 1 : 0)) * 4;
    const bool     interlace = (r.spg_control >> 4) & 1;
    const bool     dbl       = !interlace && ((r.fb_r_ctrl >> 1) & 1);
    width  = words * 4 / bpp;
    height = lines * (interlace || dbl ? 2 : 1);
    argb.assign(size_t(width) * height, 0xFF000000);
    const bool on = (r.fb_r_ctrl & 1) && !((r.vo_control >> 3) & 1);
    if (!on) return;  // leitura desligada ou blank: preto
    const uint32_t sof1 = r.fb_r_sof1 & 0x00FFFFFC, sof2 = r.fb_r_sof2 & 0x00FFFFFC;
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t base;
        if (interlace) base = ((y & 1) ? sof2 : sof1) + (y >> 1) * stride;
        else base = sof1 + (dbl ? y >> 1 : y) * stride;
        uint32_t* row = argb.data() + size_t(y) * width;
        for (uint32_t x = 0; x < width; ++x) row[x] = pixel(mem, base + x * bpp, fmt);
    }
}

} // namespace bs::native::render::kamui
