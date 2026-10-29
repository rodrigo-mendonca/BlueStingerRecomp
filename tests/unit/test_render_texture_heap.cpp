// N8.7 (docs/N8-7-KAMUI.md, D-K2 e incremento 3b): a memoria das texturas nativa (native/render/kamui/texture_heap):
// as duas vistas (32 e 64 bits) sobre os mesmos bytes, escritas parciais e a geracao por pagina que o cache usa.
#include "test.h"

#include "render/kamui/texture_heap.h"

#include <cstring>

using bs::native::render::kamui::TextureHeap;

BS_TEST(texture_heap_64_bit_path_interleaves_words_between_banks) {
    TextureHeap h;
    // offsets 0..3 do caminho de 64 bits ficam no banco 0 (armazenamento 0..3), 4..7 no banco 1 (4 MB + 0..3)
    BS_CHECK(TextureHeap::offset64(0) == 0 && TextureHeap::offset64(4) == TextureHeap::kSize / 2);
    BS_CHECK(TextureHeap::offset64(8) == 4 && TextureHeap::offset64(13) == TextureHeap::kSize / 2 + 5);
    const uint32_t w = 0x11223344;
    h.write(TextureHeap::offset64(12), &w, 4);  // o jogo escreve pela vista de 64 bits
    uint32_t r;
    std::memcpy(&r, h.data() + TextureHeap::kSize / 2 + 4, 4);  // e le pela de 32 bits: o mesmo byte
    BS_CHECK(r == w);
}

BS_TEST(texture_heap_generation_follows_partial_writes_in_both_banks) {
    TextureHeap h;
    h.reset();
    const uint64_t g0 = h.generation64(0x1000, 2048);  // uma textura de 2 KB no caminho de 64 bits
    const uint8_t  b  = 0x5A;
    h.write(TextureHeap::offset64(0x9000), &b, 1);  // longe: outra pagina
    BS_CHECK(h.generation64(0x1000, 2048) == g0);
    h.write(TextureHeap::offset64(0x1000 + 2047), &b, 1);  // o ultimo byte da textura (banco 1), escrita de 8 bits
    const uint64_t g1 = h.generation64(0x1000, 2048);
    BS_CHECK(g1 > g0);
    const uint16_t hw = 0xBEEF;
    h.write(TextureHeap::offset64(0x1000), &hw, 2);  // o primeiro (banco 0), escrita de 16 bits
    BS_CHECK(h.generation64(0x1000, 2048) > g1);
    // escrita por fora (render no framebuffer): touch marca
    const uint64_t g2 = h.generation64(0x1000, 2048);
    h.data()[TextureHeap::offset64(0x1004)] = 1;
    h.touch(TextureHeap::offset64(0x1004), 1);
    BS_CHECK(h.generation64(0x1000, 2048) > g2);
}

// revisao Codex 20261029-013355: uma textura que da a volta no fim dos 8 MB tem a parte do inicio na geracao
BS_TEST(texture_heap_generation_and_touch_wrap_at_the_end) {
    TextureHeap h;
    h.reset();
    const uint64_t g0 = h.generation64(0x7FFFF8, 128);  // 8 bytes no fim e 120 no inicio do caminho de 64 bits
    const uint8_t  b  = 1;
    h.write(TextureHeap::offset64(0x70), &b, 1);  // so na parte depois da volta
    BS_CHECK(h.generation64(0x7FFFF8, 128) > g0);
    // touch e write que atravessam o fim marcam as paginas do inicio
    const uint64_t g1 = h.generation64(0, 8);
    const uint8_t  w[4] = {1, 2, 3, 4};
    h.write(TextureHeap::kSize - 2, w, 4);
    BS_CHECK(h.data()[TextureHeap::kSize - 1] == 2 && h.data()[0] == 3 && h.data()[1] == 4);
    BS_CHECK(h.generation64(0, 8) > g1);
}
