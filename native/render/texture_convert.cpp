#include "render/texture_convert.h"

#include <cstring>

namespace bs::native::render {

namespace {
uint32_t expand(uint32_t v, int bits) {  // replica os bits altos (5/6/4 -> 8), como o hardware
    v <<= 8 - bits;
    return v | (v >> bits);
}
bool pow2(uint32_t v) { return v && !(v & (v - 1)); }
bool dims_ok(uint32_t w, uint32_t h) { return w >= 8 && h >= 8 && w <= 1024 && h <= 1024; }
uint16_t rd16(std::span<const uint8_t> d, size_t off) {
    uint16_t v;
    std::memcpy(&v, d.data() + off, 2);
    return v;
}
// Bytes antes do nivel de lado `w` numa cadeia de mipmaps (pvr_mip_offset do legado): 16 bits: 6 + 2T com
// T = (w*w - 1)/3 texels nos niveis menores (o 1x1 fica em +6); VQ (indices): 1 + ((w/2)^2 - 1)/3
uint32_t mip_offset16(uint32_t w) { return w < 2 ? 6 : 6 + 2 * ((w * w - 1) / 3); }
uint32_t mip_offset_vq(uint32_t w) { return w < 2 ? 0 : 1 + ((w / 2) * (w / 2) - 1) / 3; }
} // namespace

uint32_t texel16_to_argb(uint16_t t, Texel16 f) {
    uint32_t a, r, g, b;
    switch (f) {
    case Texel16::Argb1555:
        a = (t & 0x8000) ? 0xFF : 0, r = expand((t >> 10) & 0x1F, 5), g = expand((t >> 5) & 0x1F, 5), b = expand(t & 0x1F, 5);
        break;
    case Texel16::Rgb565:
        a = 0xFF, r = expand((t >> 11) & 0x1F, 5), g = expand((t >> 5) & 0x3F, 6), b = expand(t & 0x1F, 5);
        break;
    default:
        a = expand((t >> 12) & 0xF, 4), r = expand((t >> 8) & 0xF, 4), g = expand((t >> 4) & 0xF, 4), b = expand(t & 0xF, 4);
        break;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t twiddle_index(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const uint32_t m   = w < h ? w : h;
    uint32_t       idx = 0;
    for (uint32_t bit = 0, s = 1; s < m; s <<= 1, ++bit) {
        idx |= ((y >> bit) & 1u) << (2 * bit);
        idx |= ((x >> bit) & 1u) << (2 * bit + 1);
    }
    const uint32_t block = (w > h ? x / m : y / m);  // blocos quadrados na dimensao maior
    return block * m * m + idx;
}

bool convert16(std::span<const uint16_t> src, uint32_t w, uint32_t h, Texel16 f, bool twiddled, std::vector<uint32_t>& out) {
    // o PVR so tem texturas de 8 a 1024 por lado, potencias de 2 (twiddled exige isso)
    if (!dims_ok(w, h) || (twiddled && (!pow2(w) || !pow2(h)))) return false;
    if (src.size() < size_t(w) * h) return false;
    out.resize(size_t(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            out[size_t(y) * w + x] = texel16_to_argb(src[twiddled ? twiddle_index(x, y, w, h) : size_t(y) * w + x], f);
    return true;
}

bool convert16_mips(std::span<const uint8_t> data, uint32_t w, Texel16 f, std::vector<MipLevel>& out) {
    if (!dims_ok(w, w) || !pow2(w)) return false;
    if (data.size() < size_t(mip_offset16(w)) + 2ull * w * w) return false;
    out.clear();
    for (uint32_t s = w; s >= 1; s /= 2) {
        MipLevel       l;
        const uint32_t base = mip_offset16(s);
        l.size = s;
        l.argb.resize(size_t(s) * s);
        for (uint32_t y = 0; y < s; ++y)
            for (uint32_t x = 0; x < s; ++x) l.argb[size_t(y) * s + x] = texel16_to_argb(rd16(data, base + 2ull * twiddle_index(x, y, s, s)), f);
        out.push_back(std::move(l));
    }
    return true;
}

uint32_t vq_codebook_entries(uint32_t bytes, uint32_t w, uint32_t h, bool mipmap) {
    if (!dims_ok(w, h) || !pow2(w) || !pow2(h) || (mipmap && w != h)) return 0;
    const uint64_t idx = (mipmap ? mip_offset_vq(w) : 0) + uint64_t(w / 2) * (h / 2);
    if (bytes <= idx || (bytes - idx) % 8) return 0;
    const uint64_t n = (bytes - idx) / 8;
    return n >= 1 && n <= 256 ? uint32_t(n) : 0;
}

bool convert_vq(std::span<const uint8_t> data, uint32_t w, uint32_t h, Texel16 f, bool mipmap, uint32_t entries,
                std::vector<uint32_t>& out) {
    if (!dims_ok(w, h) || !pow2(w) || !pow2(h) || (mipmap && w != h) || entries < 1 || entries > 256) return false;
    const size_t idxBase = size_t(entries) * 8 + (mipmap ? mip_offset_vq(w) : 0);
    if (data.size() < idxBase + size_t(w / 2) * (h / 2)) return false;
    out.resize(size_t(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t code = data[idxBase + twiddle_index(x / 2, y / 2, w / 2, h / 2)];
            if (code >= entries) return false;  // indice fora de um codebook pequeno: arquivo invalido
            const uint32_t sub = ((x & 1) << 1) | (y & 1);
            out[size_t(y) * w + x] = texel16_to_argb(rd16(data, size_t(code) * 8 + sub * 2), f);
        }
    return true;
}

bool convert_pal4(std::span<const uint8_t> data, uint32_t w, uint32_t h, std::span<const uint32_t> palette,
                  std::vector<uint32_t>& out) {
    if (!dims_ok(w, h) || !pow2(w) || !pow2(h) || palette.size() < 16) return false;
    if (data.size() < size_t(w) * h / 2) return false;
    out.resize(size_t(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t i = twiddle_index(x, y, w, h);
            const uint8_t  b = data[i / 2];
            out[size_t(y) * w + x] = palette[(i & 1) ? (b >> 4) : (b & 0xF)];
        }
    return true;
}

bool convert_pal8(std::span<const uint8_t> data, uint32_t w, uint32_t h, std::span<const uint32_t> palette,
                  std::vector<uint32_t>& out) {
    if (!dims_ok(w, h) || !pow2(w) || !pow2(h) || palette.size() < 256) return false;
    if (data.size() < size_t(w) * h) return false;
    out.resize(size_t(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) out[size_t(y) * w + x] = palette[data[twiddle_index(x, y, w, h)]];
    return true;
}

} // namespace bs::native::render
