#pragma once
// N6 fase C (docs/N6-PLANO.md secao 13): os efeitos do jogo direto do BS_SND.MLT do disco do usuario (o jogo
// instalado ja tem data\disc\BSDATA), por (banco, pedido) = o par que o jogo passa ao SDK (id/128, id%128).
// Sem conversao pelo Builder: os bytes originais sao a fonte e o ADPCM da AICA e decodificado na hora; mods
// trocam por ID no SoundCatalog ("efeito/<banco>/<pedido>"). Portavel.
//
// Formato, conferido contra o oraculo O6 (registradores da AICA no key-on dos IDs 117 e 90):
//   SMLT  "SMLT", +8 n unidades; unidades em +0x20 (0x20 bytes): tipo, banco, endereco na RAM de som, tamanho,
//         deslocamento no arquivo, tamanho no arquivo
//   SOSB  "SOSB", versao, tamanho, n pedidos, n u32 com o deslocamento de cada descritor
//   SOSP  descritor (0x38 bytes): +4 SA[23:16], +5 indicadores, +6 SA[15:0] (relativo a unidade), +8 LSA,
//         +10 LEA (amostras), +16 OCT (bits 14-11, com sinal) e FNS (bits 9-0)
//   taxa = 44100 * 2^OCT * (1 + FNS/1024) Hz (AICA)
// PENDENTE (oraculo): os indicadores 1/3 (hipotese: bit 0 ADPCM, bit 1 laco; os medidos tinham 1, ADPCM sem
// laco). Um descritor com o bit 0 desligado e recusado (formato sem prova).
#include "audio/mixer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bs::native::audio {

struct SosbEffect {
    uint8_t  bank = 0, request = 0;
    uint32_t offset = 0;      // no arquivo MLT
    uint32_t samples = 0;     // LEA
    uint32_t loopStart = 0;   // LSA
    uint8_t  flags = 0;       // indicadores: bit 0 ADPCM, bit 1 laco (LPCTL; O6: 3 com laco, 2 sem, igual ao bit)
    uint8_t  baseTl = 30;     // byte 0x19: TL do canal com volume 0 (O6: TL = base - 2v)
    // O descritor e uma imagem dos registradores do canal da AICA em palavras de 16 bits (+0x08 LSA, +0x0A LEA, +0x0C
    // envelope 0x10, +0x0E envelope 0x14, +0x10 tom 0x18, +0x14 envio ao DSP 0x20, +0x16 0x24, +0x18 0x28 com o TL no byte
    // alto = o 0x19 acima): fase G
    uint16_t eg10 = 0, eg14 = 0, dsp20 = 0, reg24 = 0;  // 0x24: DISDL 11-8, DIPAN 4-0
    int      oct = 0;
    uint32_t fns = 0;
    double   rate() const;
    uint32_t id() const { return uint32_t(bank) * 128 + request; }  // o ID do jogo
};

class SosbBanks {
public:
    // nullopt: nao e um MLT com unidades SOSB coerentes (o motivo em *err)
    static std::optional<SosbBanks> parse(std::vector<uint8_t> mlt, std::string* err = nullptr);
    const SosbEffect* find(uint8_t bank, uint8_t request) const;
    size_t            size() const { return effects_.size(); }
    // PCM 16 mono na taxa do efeito; nullptr se o efeito nao existe ou o formato nao e o provado
    std::shared_ptr<SoundData> decode(uint8_t bank, uint8_t request, std::string* err = nullptr) const;

private:
    std::vector<uint8_t>    data_;
    std::vector<SosbEffect> effects_;
};

} // namespace bs::native::audio
