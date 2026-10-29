#pragma once
// N8.7 (docs/N8-7-KAMUI.md, incremento 4c): a entrada da area 4 (0x10000000-0x13FFFFFF), por onde as store queues e a
// copia do canal 2 (4b) entregam blocos de 32 bytes. Decide o caminho de cada bloco e o que ele faz:
//   - FIFO de poligonos (bits 23-24 em 0): as listas do Kamui, para o ListSink; o fim de lista acende ISTNRM (opaca bit
//     7, modificador opaco 8, translucida 9, modificador translucido 10, punch-through 21);
//   - conversor YUV (bit 23): so o filme legado o usa; no produto e um componente removido (N8.5);
//   - caminho direto de textura (bit 24): a memoria das texturas (TextureHeap, 3b) pelo caminho de 64 bits.
// Nao guarda estado nem toca a memoria: quem chama aplica o resultado (memoria das texturas, banco de status, erro
// explicito).
#include <cstdint>

namespace bs::native::render::kamui {

struct Area4Entry {
    enum class Path { Outside, Polygon, Yuv, Texture };

    // o caminho de um destino (endereco SH-4; os bits 29-31 sao o espelho e nao contam)
    static Path route(uint32_t target);
    // bit de ISTNRM do fim de uma lista (0..4); 0 fora disso
    static uint32_t list_end_bit(int list);
    // offset no armazenamento (layout de 32 bits) de cada palavra de um bloco do caminho de textura: as 8 palavras ficam
    // 4 em cada banco, contiguas (16 bytes em cada)
    // (o chamador copia as palavras e marca as duas faixas, a partir de out[0] e de out[1])
    static void texture_offsets(uint32_t target, uint32_t out[8]);
};

} // namespace bs::native::render::kamui
