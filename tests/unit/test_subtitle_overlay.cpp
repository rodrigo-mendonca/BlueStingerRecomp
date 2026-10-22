// N7c: legendas das falas por cima do gameplay (app/subtitle_overlay): o texto aparece embaixo, dentro da area do
// jogo, sem tocar a imagem fora dele; sem legenda o quadro sai identico; a mistura pre-multiplicada.
#include "test.h"

#include "subtitle_overlay.h"

#include <cstdint>
#include <vector>

namespace {
bs::GameFrame gray(uint32_t w, uint32_t h) {
    bs::GameFrame f;
    f.width = w, f.height = h;
    f.argb.assign(size_t(w) * h, 0xFF404040u);
    return f;
}
// pixels "de texto" (brancos) numa faixa de linhas
size_t whites(const bs::GameFrame& f, uint32_t y0, uint32_t y1) {
    size_t n = 0;
    for (uint32_t y = y0; y < y1; ++y)
        for (uint32_t x = 0; x < f.width; ++x) n += (f.argb[size_t(y) * f.width + x] & 0xFFFFFF) >= 0xE0E0E0;
    return n;
}
} // namespace

BS_TEST(subtitle_overlay_draws_bottom_text_only) {
    bs::SubtitleOverlay o;
    if (!o.ok()) BS_SKIP("DirectWrite/Direct2D");
    bs::GameFrame f = gray(640, 480);
    BS_CHECK(o.composed(f).argb == f.argb);  // sem legenda: identico
    f.subtitle = L"A subtitle line drawn for this test.";  // texto proprio (nao do jogo)
    const bs::GameFrame c = o.composed(f);
    BS_CHECK(whites(c, 360, 480) > 300);  // o texto, embaixo
    BS_CHECK(whites(c, 0, 240) == 0);     // nada na metade de cima
    BS_CHECK(f.argb == gray(640, 480).argb);  // o quadro original nao muda
    // tamanho e fundo (opcoes do jogador): texto maior cobre mais; o fundo escurece a faixa da legenda
    bs::native::video::OverlayStyle big;
    big.scale = 2.0f, big.box = true;
    o.set_style(big);
    const bs::GameFrame b = o.composed(f);
    BS_CHECK(whites(b, 300, 480) > whites(c, 300, 480));
    size_t darker = 0;
    for (uint32_t y = 360; y < 480; ++y)
        for (uint32_t x = 0; x < 640; ++x) darker += (b.argb[size_t(y) * 640 + x] & 0xFF) < 0x40;
    BS_CHECK(darker > 1000);
}

BS_TEST(subtitle_overlay_premultiplied_blend) {
    std::vector<uint32_t> dst = {0xFF404040u, 0xFF404040u, 0xFF404040u};
    const std::vector<uint32_t> src = {0x00000000u, 0xFFFFFFFFu, 0x80404040u};  // transparente, opaco, meio (pre-mult.)
    bs::blend_premultiplied(dst, src);
    BS_CHECK(dst[0] == 0xFF404040u && dst[1] == 0xFFFFFFFFu);
    BS_CHECK(dst[2] == 0xFF606060u);  // 0x40 + 0x40 * 127 / 255 = 64 + 32
}
