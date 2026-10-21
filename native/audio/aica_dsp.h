#pragma once
// N6 fase G: o DSP de efeitos da AICA (so este bloco, nao a AICA inteira), escrito pela especificacao publica (notas
// de Neill Corlett sobre a AICA; nenhum codigo de emulador copiado). Roda o programa que o jogo carrega (SFPB do
// BS_SND.MLT: imagem dos registradores 0x3000-0x3BFF, conferida com o oraculo) sobre os 16 barramentos de envio MIXS
// e devolve as 16 saidas EFREG, uma amostra de 44 100 Hz por chamada.
//
// O programa do Blue Stinger (docs/N6-PLANO.md secao 19) e uma reverberacao estereo: le MIXS 0 e 1 e escreve EFREG 0-3,
// que o driver mistura com EFSDL 15 e pans alternados (SFOB). Mods podem trocar o programa (mesmo formato).
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bs::native::audio {

struct AicaDspProgram {
    std::array<int16_t, 128>  coef{};   // COEF: 13 bits com sinal (o registrador >> 3)
    std::array<uint16_t, 64>  madrs{};  // MADRS: deslocamentos na memoria, em palavras
    std::array<uint64_t, 128> mpro{};   // MPRO: 64 bits por passo (as quatro palavras, a mais alta primeiro)
    uint8_t                   rbl = 0;  // tamanho do anel: 8 K << rbl palavras (0x2804 bits 14-13)
    std::array<uint8_t, 16>   efsdl{};  // nivel de cada saida EFREG (0 = muda, 15 = 0 dB, 3 dB por passo)
    std::array<uint8_t, 16>   efpan{};  // pan de cada saida (como o DIPAN)
};

// SFPB (programa) e SFOB (saidas) das unidades do MLT. O SFPB tem os registradores a partir do byte 0x54 (0x3000) e o
// RBL no byte 0x34; o SFOB tem (EFSDL, EFPAN) por saida a partir do byte 0x18.
std::optional<AicaDspProgram> parse_aica_dsp(std::span<const uint8_t> sfpb, std::span<const uint8_t> sfob,
                                             std::string* err = nullptr);

// O mesmo a partir do arquivo MLT inteiro (BS_SND.MLT): acha as unidades SFPB e SFOB.
std::optional<AicaDspProgram> parse_aica_dsp_mlt(std::span<const uint8_t> mlt, std::string* err = nullptr);

// Conversoes da memoria do DSP (16 bits em ponto flutuante: sinal, expoente de 4 bits, mantissa de 11).
uint16_t aica_dsp_pack(int32_t v24);
int32_t  aica_dsp_unpack(uint16_t f);

class AicaDsp {
public:
    explicit AicaDsp(const AicaDspProgram& p);
    // Uma amostra: mixs = os 16 barramentos (20 bits com sinal, ja saturados); efreg recebe as 16 saidas (16 bits).
    void        sample(const std::array<int32_t, 16>& mixs, std::array<int16_t, 16>& efreg);
    void        reset();
    const AicaDspProgram& program() const { return p_; }

private:
    AicaDspProgram        p_;
    std::vector<uint16_t> ring_;  // a memoria de trabalho (64 K palavras: o TABLE = 1 enxerga 0xFFFF)
    std::array<int32_t, 128> temp_{};
    std::array<int32_t, 32>  mems_{};
    std::array<int16_t, 16>  efreg_{};
    int32_t  acc_ = 0, frc_ = 0, adrs_ = 0, yreg_ = 0;
    uint16_t mdec_ = 0;
    int32_t  read_ = 0;  // o ultimo valor lido (MRD), que o IWT de dois passos depois guarda em MEMS
};

} // namespace bs::native::audio
