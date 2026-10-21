#pragma once
// N6: o envelope de volume de uma voz (o AEG da AICA), compartilhado pela musica (manatee_synth) e pelos efeitos
// (sd_adapter): os dois tocam amostras com os mesmos registradores 0x10/0x14 (camada SMPB bytes 0x08..0x0B; descritor
// SOSP +0x0C/+0x0E). Portavel; so o que o jogo usa (docs/N6-PLANO.md secao 19).
//
// Modelo (fase G, 2026-10-21), com a fonte de cada parte:
//   - estados: ataque (ate a atenuacao 0), decaimento 1 (ate DL << 5), decaimento 2, soltura (no key-off); a voz acaba
//     quando a atenuacao passa de 0x3BF (notas de Neill Corlett sobre a AICA; o mesmo no Flycast);
//   - taxa efetiva = taxa*2 + FNS[9] + max(0, (KRS + OCT)*2), limitada a 63 (KRS = 0xF desliga o termo da oitava): a
//     formula do Flycast, o oraculo deste projeto; MEDIDA no oraculo (EG por passo, 18 combinacoes de amostra, estado,
//     oitava e FNS[9] na M2A): bate com esta e nao com a das notas de Corlett, que soma a oitava negativa na taxa;
//   - tempo para a faixa inteira (1 024 unidades) por taxa efetiva: a tabela de tempos do manual da SCSP/AICA (Yamaha),
//     decaimento/soltura e ataque, em ms; as inclinacoes medidas no oraculo batem com ela (ex.: RR 16, FNS[9] = 0:
//     1 479 unidades/s medidas, 1 484 pela tabela); taxas 0 e 1 nao andam;
//   - ataque: as taxas que o jogo usa (AR 31 em 579 de 585 entradas; 25-28 nas outras) duram 0 a 2 ms: aqui linear em
//     unidades nesse tempo (a forma exponencial da AICA em <= 2 ms nao e audivel);
//   - volume: a atenuacao entra como AEG >> 2 em passos de TL (0,375 dB cada, o do oraculo; secao 19: as notas de Corlett
//     dao metade, divergencia registrada).
#include <array>
#include <cmath>
#include <cstdint>

namespace bs::native::audio {

struct EnvelopeParams {
    uint8_t  ar = 31, d1r = 0, d2r = 0, rr = 31, dl = 0, krs = 0;
    int      oct = 0;   // -8..7
    uint32_t fns = 0;   // 0..1023
    // A partir das imagens dos registradores 0x10 (AR 4-0, D1R 10-6, D2R 15-11) e 0x14 (RR 4-0, DL 9-5, KRS 13-10)
    static EnvelopeParams from_regs(uint16_t r10, uint16_t r14, int oct, uint32_t fns) {
        EnvelopeParams p;
        p.ar = uint8_t(r10 & 31), p.d1r = uint8_t((r10 >> 6) & 31), p.d2r = uint8_t((r10 >> 11) & 31);
        p.rr = uint8_t(r14 & 31), p.dl = uint8_t((r14 >> 5) & 31), p.krs = uint8_t((r14 >> 10) & 15);
        p.oct = oct, p.fns = fns;
        return p;
    }
    int effective_rate(uint8_t rate) const {
        int e = rate * 2;
        if (krs < 0xF) {
            e += int((fns >> 9) & 1);
            const int k = (int(krs) + oct) * 2;
            if (k > 0) e += k;
        }
        return e > 63 ? 63 : e;
    }
};

class Envelope {
public:
    enum class State : uint8_t { Attack, Decay1, Decay2, Release, Off };
    static constexpr double kEnd = 0x3BF;  // passou disto: a voz acaba

    // Tempo (ms) para 1 024 unidades por taxa efetiva (manual da SCSP/AICA): decaimento 1/2 e soltura; < 0 = parado
    static double dsr_ms(int eff) {
        static constexpr std::array<double, 64> t = {
            -1, -1, 118200, 101300, 88600, 70900, 59100, 50700, 44300, 35500, 29600, 25300, 22200, 17700, 14800, 12700,
            11100, 8900, 7400, 6300, 5500, 4400, 3700, 3200, 2800, 2200, 1800, 1600, 1400, 1100, 920, 790,
            690, 550, 460, 390, 340, 270, 230, 200, 170, 140, 110, 98, 85, 68, 57, 49,
            43, 34, 28, 25, 22, 18, 14, 12, 11, 8.5, 7.1, 6.1, 5.4, 4.3, 3.6, 3.1};
        return t[size_t(eff & 63)];
    }
    // Ataque (ms, faixa inteira), mesma fonte; 0 = imediato
    static double attack_ms(int eff) {
        static constexpr std::array<double, 64> t = {
            -1, -1, 8100, 6900, 6000, 4800, 4000, 3400, 3000, 2400, 2000, 1700, 1500, 1200, 1000, 860,
            760, 600, 500, 430, 380, 300, 250, 220, 190, 150, 130, 110, 95, 76, 63, 55,
            47, 38, 31, 27, 24, 19, 15, 13, 12, 9.4, 7.9, 6.8, 6.0, 4.7, 3.8, 3.4,
            3.0, 2.4, 2.0, 1.8, 1.6, 1.3, 1.1, 0.93, 0.85, 0.65, 0.53, 0.44, 0.40, 0.35, 0.0, 0.0};
        return t[size_t(eff & 63)];
    }

    void key_on(const EnvelopeParams& p) {
        p_ = p, state_ = State::Attack;
        level_ = 0x3FF;
        if (attack_ms(p_.effective_rate(p_.ar)) == 0.0) level_ = 0, state_ = State::Decay1;  // AR 31: imediato
    }
    void key_off() {
        if (state_ != State::Off) state_ = State::Release;
    }
    bool releasing() const { return state_ == State::Release || state_ == State::Off; }
    bool done() const { return state_ == State::Off; }
    State state() const { return state_; }
    double level() const { return level_; }  // atenuacao em unidades do AEG (0 = cheio)

    // Avanca `seconds` (o dono chama a cada tick de 4 ms). Devolve true se a atenuacao mudou.
    bool advance(double seconds) {
        const double antes = level_;
        double       resto = seconds;
        for (int guarda = 0; guarda < 4 && resto > 0 && state_ != State::Off; ++guarda) {
            if (state_ == State::Attack) {
                const double ms = attack_ms(p_.effective_rate(p_.ar));
                if (ms < 0) return level_ != antes;  // taxa 0/1: o ataque nao anda
                const double v = 1024.0 / (ms / 1000.0), falta = level_ / v;
                if (falta > resto) { level_ -= v * resto; resto = 0; break; }
                level_ = 0, resto -= falta, state_ = State::Decay1;
                continue;
            }
            const uint8_t taxa = state_ == State::Decay1 ? p_.d1r : state_ == State::Decay2 ? p_.d2r : p_.rr;
            const double  alvo = state_ == State::Decay1 ? double(p_.dl << 5) : kEnd + 1;
            if (state_ == State::Decay1 && level_ >= alvo) { state_ = State::Decay2; continue; }  // DL = 0: na hora
            const double ms = dsr_ms(p_.effective_rate(taxa));
            if (ms < 0) break;  // taxa 0/1: segura
            const double v = 1024.0 / (ms / 1000.0), falta = (alvo - level_) / v;
            if (falta > resto) { level_ += v * resto; resto = 0; break; }
            level_ = alvo, resto -= falta;
            state_ = state_ == State::Decay1 ? State::Decay2 : State::Off;  // decaimento 2 ate o fim tambem acaba
        }
        return level_ != antes;
    }
    // Ganho linear: AEG >> 2 em passos de TL de 0,375 dB (2^(-1/16) por passo)
    float gain() const { return state_ == State::Off ? 0.0f : float(std::exp2(-(level_ / 4.0) / 16.0)); }

private:
    EnvelopeParams p_;
    State          state_ = State::Off;
    double         level_ = 0x3FF;
};

} // namespace bs::native::audio
