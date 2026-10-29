#include "render/kamui/texture_heap.h"

#include <algorithm>
#include <cstring>

namespace bs::native::render::kamui {

void TextureHeap::reset() {
    std::fill(mem_.begin(), mem_.end(), uint8_t(0));
    ++clock_;
    std::fill(gen_.begin(), gen_.end(), clock_);  // tudo mudou
}

// revisao Codex 20261029-013355: os intervalos dao a volta no fim dos 8 MB (os leitores mascaram cada byte), entao
// escrita, marcacao e geracao dividem o intervalo em [off, fim) e [0, resto)
void TextureHeap::touch(uint32_t off, uint32_t n) {
    if (!n) return;
    n   = std::min<uint32_t>(n, kSize);
    off &= kSize - 1;
    ++clock_;
    const uint32_t first = std::min<uint32_t>(n, kSize - off);
    for (uint32_t p = off / kPage; p <= (off + first - 1) / kPage; ++p) gen_[p] = clock_;
    if (n > first)
        for (uint32_t p = 0; p <= (n - first - 1) / kPage; ++p) gen_[p] = clock_;
}

void TextureHeap::write(uint32_t off, const void* src, uint32_t n) {
    n   = std::min<uint32_t>(n, kSize);
    off &= kSize - 1;
    const uint32_t first = std::min<uint32_t>(n, kSize - off);
    std::memcpy(mem_.data() + off, src, first);
    if (n > first) std::memcpy(mem_.data(), static_cast<const uint8_t*>(src) + first, n - first);
    touch(off, n);
}

uint64_t TextureHeap::generation64(uint32_t off64, uint32_t n) const {
    if (!n) return 0;
    n = std::min<uint32_t>(n, kSize);
    // no caminho de 64 bits, as palavras de 4 bytes do intervalo alternam entre o banco 0 (bit 2 em 0) e o 1; em cada
    // banco o armazenamento de um trecho contiguo e contiguo. Um intervalo que da a volta vira dois trechos
    uint64_t g    = 0;
    auto     span = [&](uint32_t a, uint32_t len) {
        const uint32_t b = a + len - 1;
        for (uint32_t bank = 0; bank < 2; ++bank) {
            const uint32_t lo = offset64((a & ~7u) | (bank ? 4u : 0u)), hi = offset64((b & ~7u) | (bank ? 7u : 3u));
            for (uint32_t p = lo / kPage; p <= hi / kPage; ++p) g = std::max(g, gen_[p]);
        }
    };
    const uint32_t a     = off64 & (kSize - 1);
    const uint32_t first = std::min<uint32_t>(n, kSize - a);
    span(a, first);
    if (n > first) span(0, n - first);
    return g;
}

} // namespace bs::native::render::kamui
