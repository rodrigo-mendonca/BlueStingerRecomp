#pragma once
// N6: o envelope de volume de uma voz, compartilhado pela musica (manatee_synth) e, na fase G, pelos efeitos (sd_adapter):
// os dois tocam amostras com os mesmos parametros da AICA (as 5 palavras F81F... do descritor SOSP e da camada SMPB).
//
// Fase F (agora): so o minimo para a musica soar sem cortes secos: depois do key-off, uma soltura exponencial por tick de
// 4 ms (fator 0,85: 43 ticks = 172 ms ate -60 dB; teste audio_envelope_provisional_release). PROVISORIO: nao e a curva da
// AICA; todas as vozes entram com ganho 1 e soltam igual, sem AR/D1R/DL/D2R/RR/KRS (revisao Codex 2026-10-20, MEDIA:
// aceito, a curva da AICA e a fase G).
// Fase G (docs/N6-PLANO.md): ataque, decaimento 1/2, nivel de decaimento e soltura a partir das palavras do envelope
// (AR, D1R, DL, D2R, RR, KRS), com a prova no oraculo (o passo do Eliot, ID 32, e o key-off das notas). A interface fica:
// quem toca chama key_on/key_off e tick; o ganho vai para o mixer.
#include <cstdint>

namespace bs::native::audio {

class Envelope {
public:
    static constexpr float kReleasePerTick = 0.85f;  // PROVISORIO (fase G): a soltura minima da fase F
    static constexpr float kSilent         = 0.001f; // -60 dB: a voz pode ser liberada

    void key_on() { gain_ = 1.0f, releasing_ = false; }
    void key_off() { releasing_ = true; }
    bool releasing() const { return releasing_; }
    // Um tick do driver (4 ms). Devolve true se o ganho mudou.
    bool tick() {
        if (!releasing_) return false;
        gain_ *= kReleasePerTick;
        return true;
    }
    float gain() const { return gain_; }
    bool  done() const { return releasing_ && gain_ < kSilent; }

private:
    float gain_      = 1.0f;
    bool  releasing_ = false;
};

} // namespace bs::native::audio
