// N8.7 (docs/N8-7-KAMUI.md, incremento 4c): a entrada da area 4 nativa (native/render/kamui/area4_entry): caminho de
// cada destino, bits do fim de lista e offsets do caminho de textura.
#include "test.h"

#include "render/kamui/area4_entry.h"

using bs::native::render::kamui::Area4Entry;

// area 4 em qualquer espelho; os bits 23-24 escolhem o caminho; fora da area 4, recusado
BS_TEST(area4_entry_routes_by_region) {
    BS_CHECK(Area4Entry::route(0x10000000) == Area4Entry::Path::Polygon);
    BS_CHECK(Area4Entry::route(0xB07FFFE0) == Area4Entry::Path::Polygon);  // espelho P2, fim da FIFO
    BS_CHECK(Area4Entry::route(0x10800000) == Area4Entry::Path::Yuv);
    BS_CHECK(Area4Entry::route(0x11000000) == Area4Entry::Path::Texture);
    BS_CHECK(Area4Entry::route(0x11800000) == Area4Entry::Path::Texture);  // bit 24 com o 23: textura
    BS_CHECK(Area4Entry::route(0x13FFFFE0) == Area4Entry::Path::Texture);
    BS_CHECK(Area4Entry::route(0x14000000) == Area4Entry::Path::Outside);
    BS_CHECK(Area4Entry::route(0x0C000000) == Area4Entry::Path::Outside);
}

// opaca 7, modificador opaco 8, translucida 9, modificador translucido 10, punch-through 21; fora disso, nenhum
BS_TEST(area4_entry_list_end_bits) {
    BS_CHECK(Area4Entry::list_end_bit(0) == (1u << 7) && Area4Entry::list_end_bit(1) == (1u << 8));
    BS_CHECK(Area4Entry::list_end_bit(2) == (1u << 9) && Area4Entry::list_end_bit(3) == (1u << 10));
    BS_CHECK(Area4Entry::list_end_bit(4) == (1u << 21));
    BS_CHECK(Area4Entry::list_end_bit(-1) == 0 && Area4Entry::list_end_bit(5) == 0);
}

// as palavras pares no banco baixo e as impares no alto (4 MB adiante), contiguas; o bloco alinha em 32 bytes
BS_TEST(area4_entry_texture_offsets_split_the_banks) {
    uint32_t o[8];
    Area4Entry::texture_offsets(0x11000020 | 0x1C, o);
    BS_CHECK(o[0] == 0x10 && o[1] == 0x400010 && o[2] == 0x14 && o[3] == 0x400014);
    BS_CHECK(o[6] == 0x1C && o[7] == 0x40001C);
    Area4Entry::texture_offsets(0x117FFFE0, o);  // fim dos 8 MB
    BS_CHECK(o[0] == 0x3FFFF0 && o[7] == 0x7FFFFC);
}
