#pragma once
// N6 fase F (docs/N6-PLANO.md secao 18): o sintetizador das musicas do Manatee sobre o mixer nativo. Recebe os eventos do
// Sequencer (manatee_seq.h), escolhe as camadas do programa e toca as amostras ADPCM do banco (SMPB) no tom que o driver
// calcula. Portavel; nao e emulacao da AICA: so o que as musicas usam.
//
// Do driver (MANATEE.DRV, lido no ARM7) - provado:
//   - tom (0x14B0): acc = ajuste fino da camada (byte 0x27, com sinal, 1/256 de semitom) [+ pitch bend + afinacao do
//     canal]; i = (acc >> 8) + tecla + 0x60 - tecla base (byte 0x26); OCT/semitom pela tabela 0x3504 (i - 0x60 em
//     oitavas e semitons, -8..+7; fora disso a tabela do driver sai dos limites: aqui satura e conta); FNS =
//     floor(1024 * (2^(j/1536) - 1)), j = semitom * 128 + (acc & 0xFE) / 2 (tabela 0x35E8, igual nas 1536 entradas);
//     taxa = 44100 * 2^OCT * (1 + FNS/1024);
//   - camadas (0x0B18-0x11FC): ate 4 grupos por programa tocam juntos; em cada grupo, a 1a camada com a tecla em
//     [0x24, 0x25] e a velocidade em [0x2B, 0x2C] que conseguir voz;
//   - nivel (0x0FF4-0x107C, 0x1B54-0x1C3C): canal = ((T[cc7] + 1) * (T[cc11] + 1) >> 7) - 1, T = a curva de volume do
//     driver (0x3EC8, DriverTables); nivel = ((curva[c][vel] + 1) * (canal + 1) * (256 - TL da camada) - 1) >> 14, com
//     a curva c escolhida pela camada (byte 0x2A) entre as curvas do banco; o volume da porta (comando 0x05, guardado
//     como v + 128) soma: TL = 255 - limite(nivel + v, 0, 255); um controle 7/11 recalcula as vozes do canal. MEDIDO
//     (oraculo M2A, 2026-10-20): o TL do key-on da AICA igual a lei em 12 das 16 combinacoes mais frequentes;
//   - controles (tabela 0x1834, indice = numero): 7 volume (0x1B54), 10 pan (0x1C54), 11 expressao (0x1CE0), 32 banco
//     (0x1ED0: o slot SMPB do canal; o 0 e ignorado, 0x1A34), 64 pedal (0x20A8: valor & 0x40; ao soltar, desliga as
//     vozes ja soltas pela tecla), 121 reset (0x2758: os 16 canais da porta voltam ao estado do inicio e o pedal solta
//     como no 64); os outros nao mudam o som das 11 musicas (sem tratador ou sem uso);
//   - canal no inicio de cada tocar (0x2758): cc7 = 100, nivel do canal = 100 (direto, sem a curva, ate o 1o controle
//     7/11), cc11 = 127, pan = 0xC0 (bit 7: sem controle 10);
//   - pan (key-on 0x1080-0x111C, controle 0x1C54): posicao = o controle 10 do canal, ou, sem ele (bit 7), a do DIPAN da
//     camada (byte 0x12: 0x10 vira 0; lado esquerdo ((d - 0x10) ^ 0xFF) + 0x10, direito d + 0x10; vezes 4, 7 bits);
//     x = (limite(posicao + pan da porta, 0, 127) >> 2) - 16; DIPAN = x se x >= 0, senao 0x10 | (~x & 0xF); para o
//     mixer pela curva dos efeitos (dipan_pan, 3 dB por nivel: a da AICA documentada);
//   - DISDL = limite(nibble baixo do byte 0x13 da camada + (byte 0x24 da porta >> 3) - 16, 0, 15) (a porta: 0x80 = 0);
//     -3 dB por passo abaixo de 15 (AICA documentada), 0 = mudo.
// PROVISORIO (fase G): o envelope (as 5 palavras da camada): audio/envelope.h, compartilhado com os efeitos; por ora so
// a soltura minima no desliga (172 ms ate -60 dB), sem ataque/decaimento.
#include "audio/envelope.h"
#include "audio/manatee.h"
#include "audio/manatee_seq.h"
#include "audio/mixer.h"

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace bs::native::audio::manatee {

struct PitchReg {
    int      oct = 0;
    uint32_t fns = 0;
    bool     saturated = false;
    double   ratio() const;  // 2^OCT * (1 + FNS/1024): a taxa sobre 44100 Hz
};
// O calculo do driver (0x14B0): tecla, tecla base e ajuste fino (byte com sinal) [+ desvio em 1/256 de semitom]
PitchReg pitch_for(uint8_t key, uint8_t keyBase, int8_t fine, int32_t extra256 = 0);
// O DIPAN que o driver escreve (0x1C54): controle 10 do canal e o pan da porta (comando 0x06, byte com sinal)
uint8_t music_dipan(uint8_t position, int8_t portPan = 0);
// A posicao de pan (0..127) do DIPAN de uma camada, como o key-on do driver converte (0x1080-0x10B0)
uint8_t layer_pan_position(uint8_t layerDipan);

// Cada voz ligada, nos termos dos registradores da AICA (para comparar com os key-on do oraculo)
struct VoiceLog {
    uint32_t noteId = 0;
    uint8_t  canal = 0, key = 0, vel = 0, tl = 0, curva = 0, vol7 = 0, layerTl = 0, curvaIdx = 0, dipan = 0, sdl = 15;
    uint32_t sa = 0;  // relativo ao banco
    PitchReg pitch;
    bool     loop = false;
};

struct SynthStats {
    uint64_t notes = 0, voices = 0, refused = 0, noLayer = 0, saturatedPitch = 0, decodeFailures = 0;
};

class Synth {
public:
    static constexpr size_t kBankSlots = 16;  // a tabela de bancos do driver (0x14080, 8 bytes por slot)
    Synth(AudioMixer& mixer, const DriverTables& drv, Bus bus = Bus::Music);
    Synth(AudioMixer& mixer, const Bank& bank, const DriverTables& drv, Bus bus = Bus::Music);  // so o slot 0
    // O banco do slot (nullptr = vazio); trocar descarta as amostras decodificadas dele. As vozes que ja tocam guardam
    // a propria amostra (shared_ptr) e seguem ate acabar.
    void set_bank(uint8_t slot, const Bank* bank);
    void reset_channels();        // o estado inicial dos 16 canais (0x2758), no tocar
    void key_off_all();           // parar (0x4C30): todas as vozes entram na soltura, com ou sem pedal
    void on_event(const MusicEvent& e);
    void control_tick();          // a cada chamada do sequenciador (4 ms): soltura dos envelopes provisorios
    void set_port_volume(int v);  // comando 0x05 (byte com sinal; o jogo usa -16 como normal), no nivel do driver
    void set_port_pan(int8_t v);  // comando 0x06
    void stop_all();
    std::function<void(const VoiceLog&)> onVoice;  // diagnostico (bs-musica)
    const SynthStats& stats() const { return stats_; }
    size_t            sounding() const { return voices_.size(); }
    size_t            cached() const { return cache_.size(); }  // amostras decodificadas (uma por descritor)

private:
    struct Voice {
        VoiceId  id;
        uint32_t noteId;
        uint8_t  canal;
        int      level;    // o nivel do driver (0..255) sem o volume da porta
        bool     held = false;  // desligada com o pedal 64 abaixado: espera
        Envelope env;           // o envelope compartilhado (audio/envelope.h)
        uint8_t  curva = 0, layerTl = 0;  // para recalcular o nivel num controle 7/11
        uint8_t  layerPan = 0;            // posicao do DIPAN da camada (sem controle 10)
        float    sdlGain = 1.0f;
    };
    struct Canal {
        uint8_t program = 0, volume = 100, expression = 127, pan = 0xC0, level = 100;  // 0x2758
        uint8_t bank = 0;  // controle 32 (0x1ED0); o reset nao mexe
        bool    sustain = false;
    };
    std::shared_ptr<const SoundData> sample(uint8_t slot, const Bank& bank, const Layer& l);
    void                             release_pedal(uint8_t canal, bool todos);
    void                             apply(Voice& v);
    int                              channel_level(const Canal& c) const;
    uint8_t                          dipan_of(const Voice& v) const;

    AudioMixer&                                       mixer_;
    const Bank*                                       banks_[kBankSlots] = {};
    uint32_t                                          gen_[kBankSlots] = {};  // geracao do slot (chave do cache)
    const DriverTables&                               drv_;
    Bus                                               bus_;
    Canal                                             canais_[16];
    std::vector<Voice>                                voices_;
    // por amostra inteira (SA, formato/laco, LSA, LEA): duas camadas com o mesmo SA e outro descritor nao dividem a
    // decodificacao (nos 11 bancos do disco nao ha; um banco de mod pode ter; revisao Codex 2026-10-20, MEDIA)
    struct Chave {
        uint8_t  slot;
        uint32_t gen, sa;
        uint8_t  flags;
        uint16_t lsa, lea;
        auto     operator<=>(const Chave&) const = default;
    };
    std::map<Chave, std::shared_ptr<const SoundData>> cache_;
    int                                               portVolume_ = 0;
    int8_t                                            portPan_    = 0;
    SynthStats                                        stats_;
};

} // namespace bs::native::audio::manatee
