// Saida de video do PVR (ver pvr_video.h). Os enderecos de FB_R_SOF* estao no caminho de
// 32 bits da VRAM, o mesmo layout de vram_data() (e o que o render grava em FB_W_SOF1).
#include "pvr/pvr_video.h"

#include "pvr/pvr_regs.h"
#include "pvr/vram.h"

#include <algorithm>
#include <cstring>

namespace bs {
namespace {

constexpr uint32_t R(uint32_t off) { return 0x005F8000 + off; }

PvrVblankHook g_hook    = nullptr;
void*         g_hookArg = nullptr;

uint32_t expand5(uint32_t v) { return (v << 3) | (v >> 2); }
uint32_t expand6(uint32_t v) { return (v << 2) | (v >> 4); }

uint32_t pixel(const uint8_t* vram, uint32_t addr, uint32_t fmt) {
    auto at = [&](uint32_t a) { return vram[a & (kVramSize - 1)]; };
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

void pvr_video_frame(std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height) {
    const uint32_t ctrl = pvr_reg(R(0x044)), size = pvr_reg(R(0x05C));
    const uint32_t fmt = (ctrl >> 2) & 3, bpp = fmt == 2 ? 3 : (fmt == 3 ? 4 : 2);
    const uint32_t words = (size & 0x3FF) + 1, lines = ((size >> 10) & 0x3FF) + 1, mod = (size >> 20) & 0x3FF;
    const uint32_t stride    = (words + mod - (mod ? 1 : 0)) * 4;
    const bool     interlace = (pvr_reg(R(0x0D0)) >> 4) & 1;
    const bool     dbl       = !interlace && ((ctrl >> 1) & 1);
    width  = words * 4 / bpp;
    height = lines * (interlace || dbl ? 2 : 1);
    argb.assign(size_t(width) * height, 0xFF000000);
    const bool on = (ctrl & 1) && !((pvr_reg(R(0x0E8)) >> 3) & 1);
    if (!on) return;  // video desligado ou em blank: preto
    const uint8_t* vram = vram_data();
    const uint32_t sof1 = pvr_reg(R(0x050)) & 0x00FFFFFC, sof2 = pvr_reg(R(0x054)) & 0x00FFFFFC;
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t base;
        if (interlace) base = ((y & 1) ? sof2 : sof1) + (y >> 1) * stride;
        else base = sof1 + (dbl ? y >> 1 : y) * stride;
        uint32_t* row = argb.data() + size_t(y) * width;
        for (uint32_t x = 0; x < width; ++x) row[x] = pixel(vram, base + x * bpp, fmt);
    }
}

void pvr_set_vblank_hook(PvrVblankHook fn, void* arg) {
    g_hook    = fn;
    g_hookArg = arg;
}

void pvr_video_vblank() {
    if (g_hook) g_hook(g_hookArg);
}

} // namespace bs
