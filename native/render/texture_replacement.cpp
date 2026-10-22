#include "render/texture_replacement.h"

#include <cstdio>
#include <cstdlib>

namespace bs::native::render {

std::string TextureId::text() const {
    char b[48];
    std::snprintf(b, sizeof b, "%ux%u-%016llx", width, height, static_cast<unsigned long long>(content));
    return b;
}

TextureId texture_id(uint32_t width, uint32_t height, std::span<const uint32_t> argb) {
    uint64_t h = 0xCBF29CE484222325ull;  // FNV-1a 64
    auto     mix = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) h = (h ^ ((v >> (8 * i)) & 0xFF)) * 0x100000001B3ull;
    };
    mix(width), mix(height);
    for (uint32_t t : argb) mix(t);
    return {width, height, h};
}

std::optional<TextureId> parse_texture_id(const std::string& text) {
    unsigned           w = 0, h = 0;
    unsigned long long c = 0;
    int                n = 0;
    if (std::sscanf(text.c_str(), "%ux%u-%16llx%n", &w, &h, &c, &n) != 3 || size_t(n) != text.size() || !w || !h)
        return std::nullopt;
    const TextureId id{w, h, c};
    if (id.text() != text) return std::nullopt;  // so a forma canonica (16 hex minusculos, sem zeros a esquerda no tamanho)
    return id;
}

bool replacement_compatible(const TextureId& id, const ReplacementTexture& r) {
    if (!id.width || !id.height || r.width % id.width || r.height % id.height) return false;
    const uint32_t f = r.width / id.width;
    return f >= 1 && f <= 16 && r.height / id.height == f && r.argb.size() == size_t(r.width) * r.height;
}

} // namespace bs::native::render
