#include "video/movie_color.h"

#include <algorithm>

namespace bs::native::video {

void MovieColor::set(const MovieColorModel& m, uint32_t base, uint32_t offset) {
    identity_ = true;
    for (int v = 0; v < 256; ++v) {
        double x = m.tvRange ? 16.0 + v * 219.0 / 255.0 : double(v);
        if (m.modulateBase) x *= double(base & 0xFF) / 255.0;
        if (m.additiveOffset) x += double(offset & 0xFF);
        lut_[size_t(v)] = uint8_t(std::clamp(x + 0.5, 0.0, 255.0));
        identity_ = identity_ && lut_[size_t(v)] == v;
    }
}

void MovieColor::apply(std::span<uint32_t> argb) const {
    if (identity_) return;
    for (uint32_t& p : argb)
        p = (p & 0xFF000000u) | uint32_t(lut_[(p >> 16) & 0xFF]) << 16 | uint32_t(lut_[(p >> 8) & 0xFF]) << 8 | lut_[p & 0xFF];
}

} // namespace bs::native::video
