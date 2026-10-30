#pragma once
// N8.7 (docs/N8-7-KAMUI.md, passo 5b): o system block do Holly (0x005F6800-0x005F7FFF) que sobra no produto depois dos
// cortes do N8.3 ao N8.7, sem os perifericos (o Maple fica no barramento do N8.4, consultado antes) e sem o banco de
// status (0x005F6900-0x005F693F, o EventStatus do 4a, consultado antes). Guarda o ultimo valor escrito de cada
// registrador e decide o que o acesso pede:
//   - SB_SBREV 0x005F689C: revisao do barramento, 0x0000000B (so leitura);
//   - 0x005F7000-0x005F70FF (drive de GD-ROM) e 0x005F7400-0x005F74FF (DMA do G1): componentes removidos no N8.3, salvo a
//     escrita de GDST (0x005F7418) = 0 pela tabela de inicializacao do SDK (DMA parado);
//   - SB_ADSTAG/E1STAG/E2STAG/DDSTAG (0x005F7818/38/58/78) com o bit 0: a partida do DMA do G2, removido no N8.6;
//   - SB_C2DST 0x005F6808 com o bit 0: a partida da copia do canal 2 (4b) com SB_C2DSTAT e SB_C2DLEN.
// Os outros (SB_FFST 0x005F688C e a configuracao dos DMAs) devolvem o ultimo valor escrito; a primeira leitura de cada um
// e marcada, para o registro de quem chama.
#include <array>
#include <cstdint>

namespace bs::native::render::kamui {

class SystemBlock {
public:
    static constexpr uint32_t kBase = 0x005F6800, kSize = 0x1800;
    static constexpr uint32_t kSbRev = 0x005F689C, kSbRevValue = 0x0000000B;
    static constexpr uint32_t kC2Dstat = 0x005F6800, kC2Dlen = 0x005F6804, kC2Dst = 0x005F6808;
    static constexpr uint32_t kGdBase = 0x005F7000, kGdEnd = 0x005F7100, kG1Base = 0x005F7400, kG1End = 0x005F7500;
    static constexpr uint32_t kG1Gdst = 0x005F7418;

    enum class Region : uint8_t { None, GdRom, G1 };
    // o acesso cai num componente removido (o chamador encerra com o nome dele); None = nao removido
    static Region removed(uint32_t p, unsigned size, bool write, uint32_t v);
    // registrador deste bloco (32 bits, dentro da faixa)
    static bool contains(uint32_t p, unsigned size) { return p >= kBase && p < kBase + kSize && size == 4; }

    struct Read {
        uint32_t value = 0;
        bool     first = false;  // primeira leitura de um registrador sem modelo proprio (o valor e o ultimo escrito)
    };
    enum class Action : uint8_t { None, Ch2Start, G2Start };

    void   reset();
    Read   read(uint32_t p);
    Action write(uint32_t p, uint32_t v);  // guarda o valor (SB_SBREV nao) e diz a partida pedida

    uint32_t  reg(uint32_t p) const { return regs_[(p - kBase) / 4]; }
    // o registrador para a copia do canal 2, que no fim avanca SB_C2DSTAT e zera SB_C2DLEN e SB_C2DST (4b)
    uint32_t& at(uint32_t p) { return regs_[(p - kBase) / 4]; }

private:
    std::array<uint32_t, kSize / 4> regs_{};
    std::array<bool, kSize / 4>     reported_{};
};

} // namespace bs::native::render::kamui
