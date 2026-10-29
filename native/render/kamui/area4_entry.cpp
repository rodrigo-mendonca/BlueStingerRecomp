#include "render/kamui/area4_entry.h"

#include "render/kamui/texture_heap.h"

namespace bs::native::render::kamui {

Area4Entry::Path Area4Entry::route(uint32_t target) {
    const uint32_t p = target & 0x1FFFFFFF;
    if ((p >> 26) != 4) return Path::Outside;
    if ((p & 0x01800000) == 0) return Path::Polygon;
    return (p & 0x01000000) ? Path::Texture : Path::Yuv;
}

uint32_t Area4Entry::list_end_bit(int list) {
    constexpr uint32_t kBit[5] = {1u << 7, 1u << 8, 1u << 9, 1u << 10, 1u << 21};
    return list >= 0 && list <= 4 ? kBit[list] : 0;
}

void Area4Entry::texture_offsets(uint32_t target, uint32_t out[8]) {
    const uint32_t off = target & 0x00FFFFE0;
    for (uint32_t i = 0; i < 8; ++i) out[i] = TextureHeap::offset64(off + 4 * i);
}

} // namespace bs::native::render::kamui
