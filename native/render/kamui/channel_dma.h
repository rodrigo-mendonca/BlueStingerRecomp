#pragma once
// N8.7 (docs/N8-7-KAMUI.md, incremento 4b): a copia do canal 2, que o Kamui usa para mandar listas e texturas a area 4
// (SB_C2DST = 1). Guarda os registradores do DMAC do SH-4 (manual SH7750, cap. 14) e faz a sequencia inteira do disparo:
// blocos de 32 bytes de SAR2 para o destino, fim do canal e os registradores do system block do Holly. Nao le a memoria
// nem escreve na area 4 por conta propria: o leitor e o destino vem de quem chama (no produto, a RAM guest e a entrada
// do TA). O fim (ISTNRM bit 19) e levantado por quem chama, no banco de status (4a).
//   SARn 0xFFA00000 + 0x10n, DARn +4, DMATCRn +8, CHCRn +0xC; DMAOR 0xFFA00040.
//   CHCR bit 0 DE (habilita), bit 1 TE (fim); DMAOR bit 0 DME (habilita o controlador).
//   SB_C2DSTAT 0x005F6800 (destino), SB_C2DLEN 0x005F6804 (bytes), SB_C2DST 0x005F6808 (partida).
#include <cstdint>

namespace bs::native::render::kamui {

class ChannelDma {
public:
    static constexpr uint32_t kBase = 0xFFA00000, kDmaor = 0xFFA00040;
    static constexpr uint32_t kDe = 1u << 0, kTe = 1u << 1, kDme = 1u << 0;

    // leitura de uma palavra da origem; destino de um bloco de 32 bytes (false: fora da area 4)
    using Reader = uint32_t (*)(void* ctx, uint32_t addr);
    using Sink   = bool (*)(void* ctx, uint32_t dest, const uint32_t* blk);

    // os registradores do system block que o disparo le e atualiza (guardados por quem chama)
    struct SbRegs {
        uint32_t c2dstat = 0, c2dlen = 0, c2dst = 0;
    };
    enum class Result {
        Disabled,  // DMAOR.DME ou CHCR2.DE desligado: nada muda
        Done,      // copia feita: SAR2 avanca, DMATCR2 = 0, CHCR2.TE; C2DSTAT avanca, C2DLEN e C2DST zeram
        BadDest,   // um bloco caiu fora da area 4 (erro explicito de quem chama; nada mais e atualizado)
    };

    static bool contains(uint32_t a) { return a == kDmaor || (a >= kBase && a < kBase + 0x40 && !(a & 3)); }

    void reset();
    uint32_t reg(uint32_t a) const;  // a: contains(a)
    void     write(uint32_t a, uint32_t v);

    // SB_C2DST = 1. O FIFO de poligonos (destino com os bits 23-24 em 0) recebe todos os blocos no mesmo endereco; o
    // caminho de textura avanca 32 bytes por bloco. O comprimento que nao e multiplo de 32 copia o ultimo bloco inteiro
    Result ch2_start(SbRegs& sb, Reader read, Sink sink, void* ctx);

    uint64_t transfers() const { return transfers_; }  // copias feitas desde o inicio do processo

private:
    uint32_t ch_[4][4] = {};  // SAR, DAR, DMATCR, CHCR
    uint32_t dmaor_    = 0;
    uint64_t transfers_ = 0;
};

} // namespace bs::native::render::kamui
