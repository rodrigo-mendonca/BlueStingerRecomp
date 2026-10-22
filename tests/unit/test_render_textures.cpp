// N7 (preparacao): conversores de textura dos formatos que o jogo usa (varredura dos .NT): VQ e VQ pequeno,
// cadeia de mipmaps, paleta de 4 bits; padroes sinteticos e um caminho independente de referencia.
#include "test.h"

#include "render/texture_convert.h"

#include <cstring>
#include <vector>

namespace nr = bs::native::render;

namespace {
void put16(std::vector<uint8_t>& d, size_t off, uint16_t v) { std::memcpy(d.data() + off, &v, 2); }
// Morton independente do conversor (revisao Codex 2026-10-20, BAIXA): espalhamento de bits por mascaras, y nos
// bits pares e x nos impares; blocos quadrados lado a lado na dimensao maior
uint32_t part1by1(uint32_t n) {
    n &= 0xFFFF;
    n = (n | (n << 8)) & 0x00FF00FFu;
    n = (n | (n << 4)) & 0x0F0F0F0Fu;
    n = (n | (n << 2)) & 0x33333333u;
    return (n | (n << 1)) & 0x55555555u;
}
uint32_t ref_twiddle(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const uint32_t m = w < h ? w : h;
    return (w > h ? x / m : y / m) * m * m + (part1by1(y % m) | (part1by1(x % m) << 1));
}
uint64_t fnv(const std::vector<uint32_t>& v) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t x : v)
        for (int i = 0; i < 4; ++i) h = (h ^ ((x >> (8 * i)) & 0xFF)) * 1099511628211ull;
    return h;
}
// expansao por formula independente da do conversor (bits altos replicados = v*255/max arredondado para
// estas larguras): 5 bits (v<<3)|(v>>2), 6 bits (v<<2)|(v>>4), 4 bits v*17
uint32_t ref565(uint16_t t) {
    const uint32_t r = (t >> 11) & 31, g = (t >> 5) & 63, b = t & 31;
    return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}
uint32_t ref4444(uint16_t t) {
    return (((t >> 12) & 15) * 17u) << 24 | (((t >> 8) & 15) * 17u) << 16 | (((t >> 4) & 15) * 17u) << 8 | (t & 15) * 17u;
}
uint32_t ref1555(uint16_t t) {
    const uint32_t r = (t >> 10) & 31, g = (t >> 5) & 31, b = t & 31;
    return ((t & 0x8000) ? 0xFF000000u : 0) | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
}
} // namespace

// Os 65 536 valores dos 3 formatos que o jogo usa: hash do conversor = hash do caminho independente
BS_TEST(render_texel16_all_values_hash) {
    std::vector<uint32_t> a, b;
    for (int f = 0; f < 3; ++f) {
        a.clear(), b.clear();
        for (uint32_t t = 0; t < 65536; ++t) {
            a.push_back(nr::texel16_to_argb(uint16_t(t), nr::Texel16(f)));
            b.push_back(f == 0 ? ref1555(uint16_t(t)) : f == 1 ? ref565(uint16_t(t)) : ref4444(uint16_t(t)));
        }
        BS_CHECK(fnv(a) == fnv(b) && a == b);
    }
}

// VQ (2 KB de codebook) e VQ PEQUENO (codebook do tamanho que o arquivo da): blocos 2x2 na ordem (0,0),
// (0,1), (1,0), (1,1); indices twiddled em (w/2, h/2); com mipmap, os indices dos niveis menores antes
BS_TEST(render_vq_normal_small_and_mip) {
    const uint32_t w = 16, h = 16, blocks = 5;
    // imagem de referencia: o bloco (bx, by) usa o vetor (bx*3 + by) % 5; texel = vetor*16 + sub
    auto texel = [](uint32_t code, uint32_t sub) { return uint16_t(0x0841 * (code + 1) + sub * 0x1000); };
    for (uint32_t entries : {blocks, 256u}) {
        for (bool mip : {false, true}) {
            const uint32_t prefix = mip ? 1 + ((w / 2) * (w / 2) - 1) / 3 : 0;  // 22 no 16x16
            std::vector<uint8_t> d(entries * 8 + prefix + (w / 2) * (h / 2), 0xEE);  // lixo nos niveis menores
            for (uint32_t c = 0; c < entries; ++c)
                for (uint32_t s = 0; s < 4; ++s) put16(d, c * 8 + s * 2, texel(c % blocks, s));
            for (uint32_t by = 0; by < h / 2; ++by)
                for (uint32_t bx = 0; bx < w / 2; ++bx) d[entries * 8 + prefix + ref_twiddle(bx, by, w / 2, h / 2)] = uint8_t((bx * 3 + by) % blocks);
            BS_CHECK(nr::vq_codebook_entries(uint32_t(d.size()), w, h, mip) == entries);
            std::vector<uint32_t> out;
            BS_CHECK(nr::convert_vq(d, w, h, nr::Texel16::Rgb565, mip, entries, out));
            bool ok = out.size() == w * h;
            for (uint32_t y = 0; ok && y < h; ++y)
                for (uint32_t x = 0; x < w; ++x) {
                    const uint32_t code = ((x / 2) * 3 + y / 2) % blocks, sub = ((x & 1) << 1) | (y & 1);
                    ok &= out[y * w + x] == ref565(texel(code, sub));
                }
            BS_CHECK(ok);
        }
    }
    // recusas: indice fora do codebook pequeno, tamanho que nao fecha, dimensoes invalidas
    std::vector<uint8_t> bad(blocks * 8 + 64, 0);
    bad[blocks * 8] = 9;
    std::vector<uint32_t> out;
    BS_CHECK(!nr::convert_vq(bad, 16, 16, nr::Texel16::Rgb565, false, blocks, out));
    BS_CHECK(nr::vq_codebook_entries(blocks * 8 + 63, 16, 16, false) == 0);
    BS_CHECK(nr::vq_codebook_entries(257 * 8 + 64, 16, 16, false) == 0);
    BS_CHECK(!nr::convert_vq(bad, 16, 8, nr::Texel16::Rgb565, true, blocks, out));  // mipmap so quadrada
    BS_CHECK(!nr::convert_vq(bad, 12, 16, nr::Texel16::Rgb565, false, blocks, out));
    BS_CHECK(!nr::convert_vq(bad, 16, 16, nr::Texel16::Rgb565, false, 0, out));
}

// Mipmaps de 16 bits twiddled: niveis do menor (1x1 em +6) ao maior; o conversor devolve do maior ao 1x1
BS_TEST(render_mip_chain_16) {
    const uint32_t w = 8;
    std::vector<uint8_t> d(6 + 2 * (1 + 4 + 16) + 2 * 64, 0);
    size_t off = 6;
    for (uint32_t s = 1; s <= w; s *= 2) {
        for (uint32_t y = 0; y < s; ++y)
            for (uint32_t x = 0; x < s; ++x) put16(d, off + 2 * ref_twiddle(x, y, s, s), uint16_t(s * 0x100 + y * 16 + x));
        off += 2ull * s * s;
    }
    std::vector<nr::MipLevel> lv;
    BS_CHECK(nr::convert16_mips(d, w, nr::Texel16::Argb4444, lv));
    BS_CHECK(lv.size() == 4 && lv[0].size == 8 && lv[3].size == 1);
    bool ok = true;
    for (const auto& l : lv)
        for (uint32_t y = 0; y < l.size; ++y)
            for (uint32_t x = 0; x < l.size; ++x) ok &= l.argb[y * l.size + x] == ref4444(uint16_t(l.size * 0x100 + y * 16 + x));
    BS_CHECK(ok);
    d.pop_back();
    BS_CHECK(!nr::convert16_mips(d, w, nr::Texel16::Argb4444, lv));  // cadeia curta
}

// Paleta de 4 bits: twiddled, o indice par no nibble baixo; a paleta (16 cores) ja em ARGB8888
BS_TEST(render_pal4_twiddled_nibbles) {
    const uint32_t w = 8, h = 8;
    std::vector<uint32_t> pal(16);
    for (uint32_t i = 0; i < 16; ++i) pal[i] = 0xFF000000u | i * 0x111111u;
    std::vector<uint8_t> d(w * h / 2, 0);
    auto idx = [](uint32_t x, uint32_t y) { return (x + 2 * y) & 15; };
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t i = ref_twiddle(x, y, w, h);
            d[i / 2] |= uint8_t(idx(x, y) << ((i & 1) * 4));
        }
    std::vector<uint32_t> out;
    BS_CHECK(nr::convert_pal4(d, w, h, pal, out));
    bool ok = true;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) ok &= out[y * w + x] == pal[idx(x, y)];
    BS_CHECK(ok);
    BS_CHECK(!nr::convert_pal4(d, w, h, std::span<const uint32_t>(pal.data(), 8), out));  // paleta curta
    d.pop_back();
    BS_CHECK(!nr::convert_pal4(d, w, h, pal, out));
}

// Enderecamento twiddled do conversor contra valores calculados a mao e o Morton independente (quadrada e
// retangular nos dois sentidos)
BS_TEST(render_twiddle_known_values) {
    BS_CHECK(nr::twiddle_index(1, 0, 8, 8) == 2 && nr::twiddle_index(0, 1, 8, 8) == 1 && nr::twiddle_index(1, 1, 8, 8) == 3);
    BS_CHECK(nr::twiddle_index(2, 0, 8, 8) == 8 && nr::twiddle_index(3, 5, 8, 8) == 27);  // y=101b->17, x=011b->10
    BS_CHECK(nr::twiddle_index(9, 1, 16, 8) == 67);   // bloco 1 (64) + (x=1, y=1) = 3
    BS_CHECK(nr::twiddle_index(1, 9, 8, 16) == 67);   // retangular vertical: bloco 1 + 3
    bool ok = true;
    for (uint32_t w : {8u, 16u, 64u})
        for (uint32_t h : {8u, 32u, 64u})
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x) ok &= nr::twiddle_index(x, y, w, h) == ref_twiddle(x, y, w, h);
    BS_CHECK(ok);
}
