// N8.7 (docs/N8-7-KAMUI.md, incremento 3c): o servico de exibicao nativo (native/render/kamui/display_service): a imagem
// exibida lida da memoria das texturas pelos registradores de leitura do framebuffer.
#include "test.h"

#include "render/kamui/display_service.h"
#include "render/kamui/texture_heap.h"

#include <cstring>

using bs::native::render::kamui::DisplayRegs;
using bs::native::render::kamui::TextureHeap;
using bs::native::render::kamui::scanout;

namespace {

DisplayRegs regs(uint32_t ctrl, uint32_t words, uint32_t lines, uint32_t mod, bool interlace) {
    DisplayRegs r;
    r.fb_r_ctrl   = ctrl;
    r.fb_r_size   = (words - 1) | ((lines - 1) << 10) | (mod << 20);
    r.fb_r_sof1   = 0x1000;
    r.fb_r_sof2   = 0x2000;
    r.spg_control = interlace ? 0x10 : 0;
    return r;
}

void put16(TextureHeap& h, uint32_t off, uint16_t v) { h.write(off, &v, 2); }

} // namespace

// os registradores vem de quem tem o estado do quadro: o leitor devolve cada um pelo endereco fisico
BS_TEST(display_service_reads_the_six_registers_by_address) {
    const auto r = DisplayRegs::from([](uint32_t a) { return a & 0xFFF; });
    BS_CHECK(r.fb_r_ctrl == 0x044 && r.fb_r_sof1 == 0x050 && r.fb_r_sof2 == 0x054 && r.fb_r_size == 0x05C);
    BS_CHECK(r.spg_control == 0x0D0 && r.vo_control == 0x0E8);
}

// entrelacado (o modo do jogo): linhas pares do campo 1, impares do campo 2, passo com o modulo
BS_TEST(display_service_weaves_both_fields_with_the_modulo) {
    TextureHeap h;
    h.reset();
    put16(h, 0x1000, 0x7C00);       // 0555: campo 1, linha 0 -> y 0
    put16(h, 0x1000 + 16, 0x03E0);  // campo 1, linha 1 -> y 2 (2 + 3 - 1 = 4 palavras)
    put16(h, 0x2000, 0x001F);       // campo 2, linha 0 -> y 1
    std::vector<uint32_t> f;
    uint32_t              w, ht;
    scanout(regs(1, 2, 2, 3, true), h.data(), f, w, ht);
    BS_CHECK(w == 4 && ht == 4);
    BS_CHECK(f[0] == 0xFFFF0000 && f[4] == 0xFF0000FF && f[8] == 0xFF00FF00);
}

// formatos de 16, 24 e 32 bits, duplicacao de linha, leitura desligada e blank
BS_TEST(display_service_formats_line_double_off_and_blank) {
    TextureHeap h;
    h.reset();
    std::vector<uint32_t> f;
    uint32_t              w, ht;
    put16(h, 0x1000, 0xF800);
    scanout(regs(1 | 2 | (1u << 2), 2, 1, 1, false), h.data(), f, w, ht);  // 565 com duplicacao
    BS_CHECK(ht == 2 && f[0] == 0xFFFF0000 && f[4] == 0xFFFF0000);
    const uint8_t p3[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    h.write(0x1000, p3, 6);
    scanout(regs(1 | (2u << 2), 2, 1, 1, false), h.data(), f, w, ht);  // 888 empacotado
    BS_CHECK(w == 2 && f[0] == 0xFF030201 && f[1] == 0xFF060504);
    scanout(regs(1 | (3u << 2), 2, 1, 1, false), h.data(), f, w, ht);  // 0888
    BS_CHECK(w == 2 && f[0] == 0xFF030201);
    scanout(regs(1u << 2, 2, 1, 1, false), h.data(), f, w, ht);  // leitura desligada
    BS_CHECK(f[0] == 0xFF000000);
    auto blank       = regs(1 | (1u << 2), 2, 1, 1, false);
    blank.vo_control = 8;
    scanout(blank, h.data(), f, w, ht);
    BS_CHECK(f[0] == 0xFF000000);
}

// um framebuffer no fim dos 8 MB continua no inicio (cada byte com a volta, como a saida anterior)
BS_TEST(display_service_wraps_at_the_end_of_the_memory) {
    TextureHeap h;
    h.reset();
    put16(h, TextureHeap::kSize - 2, 0xF800);  // ultimo pixel antes da volta
    put16(h, 0, 0x07E0);                       // primeiro depois
    auto r      = regs(1 | (1u << 2), 2, 1, 1, false);
    r.fb_r_sof1 = TextureHeap::kSize - 4;  // FB_R_SOF e alinhado a 4 bytes: os pixels 1 e 2 ficam dos dois lados
    std::vector<uint32_t> f;
    uint32_t              w, ht;
    scanout(r, h.data(), f, w, ht);
    BS_CHECK(f[1] == 0xFFFF0000 && f[2] == 0xFF00FF00);
}
