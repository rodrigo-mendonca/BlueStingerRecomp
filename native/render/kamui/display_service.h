#pragma once
// N8.7 (docs/N8-7-KAMUI.md, incremento 3c): a imagem que o video mostra, lida da memoria das texturas pelos registradores
// de leitura do framebuffer que o Kamui programa. E um servico nativo sobre o estado do quadro e a memoria (os mesmos
// bytes nos mesmos enderecos), nao o hardware simulado: a mesma conta da saida de video anterior (pvr_video).
//  - FB_R_CTRL (0x044): bit 0 liga a leitura, bit 1 duplica linhas, bits 3-2 formato (0 0555, 1 565, 2 888 empacotado,
//    3 0888);
//  - FB_R_SIZE (0x05C): largura em palavras - 1 (9-0), linhas - 1 (19-10), modulo (29-20; passo de linha = largura +
//    modulo - 1 palavras);
//  - FB_R_SOF1/SOF2 (0x050/0x054): campo par/impar; com entrelacamento (SPG_CONTROL 0x0D0 bit 4) os dois campos sao
//    intercalados (weave);
//  - leitura desligada (FB_R_CTRL bit 0 = 0) ou blank (VO_CONTROL 0x0E8 bit 3): preto.
// Os enderecos de FB_R_SOF* estao no caminho de 32 bits (o layout do armazenamento de TextureHeap); cada byte e lido com
// a volta no fim dos 8 MB.
#include <cstdint>
#include <vector>

namespace bs::native::render::kamui {

struct DisplayRegs {
    uint32_t fb_r_ctrl   = 0;  // 005F8044
    uint32_t fb_r_sof1   = 0;  // 005F8050
    uint32_t fb_r_sof2   = 0;  // 005F8054
    uint32_t fb_r_size   = 0;  // 005F805C
    uint32_t spg_control = 0;  // 005F80D0
    uint32_t vo_control  = 0;  // 005F80E8

    // le os seis registradores por `reg(endereco fisico)` (o FrameState do produto ou o banco da validacao)
    template <class Reg> static DisplayRegs from(Reg&& reg) {
        DisplayRegs r;
        r.fb_r_ctrl   = reg(0x005F8044u);
        r.fb_r_sof1   = reg(0x005F8050u);
        r.fb_r_sof2   = reg(0x005F8054u);
        r.fb_r_size   = reg(0x005F805Cu);
        r.spg_control = reg(0x005F80D0u);
        r.vo_control  = reg(0x005F80E8u);
        return r;
    }
};

// O quadro exibido em ARGB8888 (alfa 0xFF) sobre `mem` (8 MB, TextureHeap::kSize). Devolve largura e altura; o vetor e
// redimensionado.
void scanout(const DisplayRegs& r, const uint8_t* mem, std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height);

} // namespace bs::native::render::kamui
