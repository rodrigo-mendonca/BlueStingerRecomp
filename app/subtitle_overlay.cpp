#include "subtitle_overlay.h"

#include <algorithm>

namespace bs {

SubtitleOverlay::SubtitleOverlay(const native::video::OverlayStyle& style)
    : raster_(native::video::make_subtitle_rasterizer()), style_(style) {}

bool SubtitleOverlay::ok() const { return raster_ && raster_->ok(); }

void SubtitleOverlay::set_style(const native::video::OverlayStyle& style) {
    style_ = style;
    valid_ = false;
}

const std::vector<uint32_t>* SubtitleOverlay::layer(const std::wstring& text, uint32_t w, uint32_t h, float cx, float cy,
                                                    float cw, float ch) {
    if (text.empty() || !ok() || !w || !h) return nullptr;
    const float area[4] = {cx, cy, cw, ch};
    if (!valid_ || text != text_ || w != w_ || h != h_ || !std::equal(area, area + 4, area_)) {
        buf_.resize(size_t(w) * h);
        raster_->draw_overlay(buf_, w, h, text, cx, cy, cw, ch, style_);
        text_ = text, w_ = w, h_ = h, valid_ = true, ++version_;
        std::copy(area, area + 4, area_);
    }
    return &buf_;
}

void blend_premultiplied(std::vector<uint32_t>& dst, const std::vector<uint32_t>& src) {
    const size_t n = std::min(dst.size(), src.size());
    for (size_t i = 0; i < n; ++i) {
        const uint32_t s = src[i];
        const uint32_t a = s >> 24;
        if (!a) continue;
        const uint32_t d = dst[i], inv = 255 - a;
        uint32_t       o = 0xFF000000u;
        for (int sh = 0; sh < 24; sh += 8) {
            const uint32_t c = ((s >> sh) & 0xFF) + (((d >> sh) & 0xFF) * inv + 127) / 255;
            o |= std::min<uint32_t>(c, 255) << sh;
        }
        dst[i] = o;
    }
}

GameFrame SubtitleOverlay::composed(const GameFrame& f) {
    GameFrame out = f;
    if (const auto* l = layer(f.subtitle, f.width, f.height, 0, 0, float(f.width), float(f.height)))
        blend_premultiplied(out.argb, *l);
    return out;
}

} // namespace bs
