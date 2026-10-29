#pragma once
// N8.7 (docs/N8-7-KAMUI.md, D-K2 e incremento 3b): a memoria das texturas, do fundo e dos framebuffers que o Kamui
// enderecado como a VRAM (8 MB). E memoria do guest, nao hardware simulado: os mesmos bytes nos mesmos enderecos, com as
// duas vistas do jogo (o caminho de 32 bits, linear, e o de 64 bits, com as palavras intercaladas entre os dois bancos),
// escritas parciais de 8/16/32 bits e uma geracao por pagina de 4 KB do armazenamento, que sobe a cada escrita. O cache
// de texturas usa a geracao para saber, sem reler os bytes, se uma textura mudou desde a ultima conversao.
#include <cstdint>
#include <vector>

namespace bs::native::render::kamui {

class TextureHeap {
public:
    static constexpr uint32_t kSize = 8u << 20;  // 8 MB
    static constexpr uint32_t kPage = 4096;      // pagina do armazenamento (layout do caminho de 32 bits)

    TextureHeap() : mem_(kSize), gen_(kSize / kPage, 0) {}

    // offset no armazenamento (layout de 32 bits) de um offset do caminho de 64 bits
    static uint32_t offset64(uint32_t o) {
        o &= kSize - 1;
        return ((o & ~7u) >> 1) | (o & 3u) | ((o & 4u) ? kSize / 2 : 0u);
    }

    void reset();
    uint8_t*       data() { return mem_.data(); }
    const uint8_t* data() const { return mem_.data(); }

    // escrita do jogo ou do render no armazenamento (offset no layout de 32 bits): copia e marca as paginas tocadas
    void write(uint32_t off, const void* src, uint32_t n);
    // marca paginas escritas por fora (um ponteiro de data()): o chamador ja copiou os bytes
    void touch(uint32_t off, uint32_t n);

    // a maior geracao das paginas que um intervalo do caminho de 64 bits ocupa (as palavras dele ficam nos dois bancos)
    uint64_t generation64(uint32_t off64, uint32_t n) const;
    uint64_t generation() const { return clock_; }  // a ultima geracao dada (0: nada escrito desde o reset)

private:
    std::vector<uint8_t>  mem_;
    std::vector<uint64_t> gen_;
    uint64_t              clock_ = 0;
};

} // namespace bs::native::render::kamui
