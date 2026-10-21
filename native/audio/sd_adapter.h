#pragma once
// N6: adaptador das 5 entradas do SDK de som que o gerenciador de efeitos do jogo chama (docs/N6-PLANO.md,
// secao 10). O gerenciador (fila de 16, prioridade, 7 ativos, rodizio de 7 canais, id/128 e id%128) continua
// recompilado; so estas chamadas trocam de dono. Portavel: sem Win32, sem o jogo e sem hardware; o tempo e o
// do chamador (sync), nunca o relogio de parede. NAO esta ligado as funcoes recompiladas.
//
// Comprovado no binario (C++ gerado + pool literal; cada entrada monta um pacote estatico e chama o envio
// 8C17D334; erro != 0 vai para 8C17DC18(endereco da propria funcao, codigo); retorno r0 = o codigo):
//   8C15E208(h)                 comando 0x12, pacote 8C2D8FC0: +4 porta. Para o canal (key-off): o consumidor
//                               chama antes de tocar; 8C02AB40 chama para parar o canal avulso
//   8C15E088(h, banco, pedido, x)  comando 0x11, pacote 8C2D8F84: +4 porta, +5 banco, +6 pedido, +7 x*8;
//                               x (byte com sinal) fora de 0..15 -> 0x03000001. O jogo passa x = 0
//   8C15E19C(h, v, t)           comando 0x15, pacote 8C2D8FAC: +4 porta, +5 (int8)v + 128, +6/+7 t (16 bits).
//                               NAO confere a inicializacao (so o handle nulo), ao contrario das outras
//   8C15E11C(h, v, t)           comando 0x16, pacote 8C2D8F98: o mesmo formato, com a conferencia
//   8C15DF44(h, out)            CONSULTA (nao para nada): out[0] = out[1] = 0 e 10 leituras do driver em
//                               out+2..+27. O jogo so le o bit 0 da palavra em +24 (8C02ACB2): 0 = canal livre
// h e o ponteiro do handle do canal; o driver usa o byte 1 (a porta). Comuns: flag de inicializacao 8C1DDB90
// == 0 -> 0x0D000001; h nulo -> 0x07000103; out nulo -> 0x07000203.
// O jogo passa t = 0 nas 3 chamadas e ignora o retorno nos dois consumidores (8C02A8C0 e 8C02AA8C).
//
// MEDIDO no oraculo O6 (Flycast, tools/dev/n6_oraculo.py --forcar, docs/N6-PLANO.md secao 12): o valor do comando
// trocado antes de executar e o efeito nos registradores da AICA no key-on do mesmo som:
//   0x15 (o `a` do jogo) = VOLUME: TL = clamp(30 - 2v, 0, 255)  (v 0 -> 30, -16 -> 62, -64 -> 158, 63 -> 0)
//   0x16 (o `b` do jogo) = PAN:    DIPAN = (v < 0 ? 0x10 : 0) | ((v < 0 ? ~v : v) >> 2)  (-64 -> 31, -32 -> 23,
//                                  0 -> 0, 16 -> 4, 63 -> 15; 0x10 = esquerda, nivel 15 = o outro lado mudo)
// PENDENTE (captura do audio da AICA): os dB por passo de TL (0,375) e de DIPAN (3, nivel 15 mudo) sao os da AICA
// documentada, nao medidos neste jogo; a unidade de t, o x do tocar e os outros campos da consulta. Tempo t != 0
// (sem uso no jogo) e recusado com um codigo do adaptador, nunca ignorado.
#include "audio/mixer.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace bs::native::audio {

namespace sd_err {
inline constexpr uint32_t kOk             = 0;
inline constexpr uint32_t kNotInitialized = 0x0D000001;
inline constexpr uint32_t kNullHandle     = 0x07000103;
inline constexpr uint32_t kNullOut        = 0x07000203;
inline constexpr uint32_t kRange          = 0x03000001;
inline constexpr uint32_t kUnsupported    = 0x7F000001;  // so do adaptador: tempo t != 0
} // namespace sd_err

// Handle do canal como o driver o ve: so a porta (byte 1 do handle original).
struct SdHandle {
    uint8_t port = 0;
};

enum class SdParamRole : uint8_t { Pan, Volume, Ignore };

// Leis medidas no O6 (registradores) com a curva da AICA (dB por passo: pendente de captura de audio).
// TL = base - 2v, com a base no descritor SOSP do efeito (byte 0x19; O6 2026-10-20: 30, 30, 40, 20, 45 nos 5 efeitos
// medidos, igual ao byte). O 30 do O6 original era a base dos IDs 117 e 90, nao uma constante do driver.
inline constexpr uint8_t kSdDefaultBaseTl = 30;
inline uint8_t sd_tl(int8_t v, uint8_t base = kSdDefaultBaseTl) {
    const int t = int(base) - 2 * int(v);
    return uint8_t(t < 0 ? 0 : t > 255 ? 255 : t);
}
inline uint8_t sd_dipan(int8_t v) { return uint8_t((v < 0 ? 0x10 : 0) | (((v < 0 ? ~v : v) >> 2) & 0xF)); }
float sd_volume_gain(int8_t v, uint8_t base = kSdDefaultBaseTl);  // 10^(-0,375 dB * TL / 20)
float sd_pan(int8_t v);          // pan do mixer (o outro lado com 10^(-3 dB * nivel / 20); nivel 15 = mudo)
float dipan_pan(uint8_t dipan);  // o mesmo a partir do DIPAN da AICA (bit 4 = esquerda, nivel 0..15); efeitos e musica

struct SdAdapterOptions {
    // Papeis medidos no O6 (o indicio estatico anterior, "pan em a", estava ERRADO: a e o volume)
    SdParamRole cmd15 = SdParamRole::Volume;
    SdParamRole cmd16 = SdParamRole::Pan;
    // Conversao do valor (byte com sinal do jogo) para o mixer pelas leis medidas (sd_volume_gain, sd_pan)
    std::function<float(int8_t)> toPan    = [](int8_t v) { return sd_pan(v); };
    std::function<float(int8_t, uint8_t)> toVolume = [](int8_t v, uint8_t base) { return sd_volume_gain(v, base); };
    Bus                          bus      = Bus::Effects;
    // Eventos do mesmo mixer que nao sao das portas (musica, voz): o adaptador e quem chama take_events, entao
    // os entrega aqui na hora. Sem callback, ficam em take_other_events() ate `otherEventsCap`; o excedente e
    // descartado e contado (other_overflow), nunca cresce sem limite (revisao Codex 2026-10-20, BAIXA).
    std::function<void(const MixerEvent&)> onOtherEvent;
    size_t                                 otherEventsCap = 1024;
};

// Som resolvido para (banco, pedido): os dados e o fim que o JOGO ve (posicao na fonte, -1 = o fim fisico;
// sound_catalog.h logical_end_frames). Uma substituicao mais longa que o original libera o canal no fim
// LOGICO e continua soando ate acabar ou ate o canal ser reutilizado/parado. data nulo = recurso ausente.
struct SdSound {
    std::shared_ptr<const SoundData> data;
    int64_t                          logicalEnd = -1;
    uint8_t                          baseTl = kSdDefaultBaseTl;  // nivel do efeito (descritor SOSP, byte 0x19)
};
using SdResolver = std::function<SdSound(uint8_t bank, uint8_t request)>;

class SdAdapter {
public:
    // O mixer deve ter vozes para todas as portas usadas: uma voz recusada conta em refused() e o canal fica livre
    SdAdapter(AudioMixer& mixer, SdResolver resolver, SdAdapterOptions o = {});

    void set_initialized(bool on) { init_ = on; }  // o flag 8C1DDB90 do original

    uint32_t stop(const SdHandle* h);                                     // 8C15E208
    uint32_t play(const SdHandle* h, int8_t bank, int8_t request, int8_t x);  // 8C15E088
    uint32_t param15(const SdHandle* h, int8_t value, uint16_t time);     // 8C15E19C
    uint32_t param16(const SdHandle* h, int8_t value, uint16_t time);     // 8C15E11C
    uint32_t status(const SdHandle* h, uint8_t* out28);                   // 8C15DF44 (28 bytes)

    // Avanca o tempo LOGICO ate o quadro de saida `upTo` (o tocado ou o do relogio do jogo): so entao um canal
    // que acabou aparece livre na consulta (mixer.h: a renderizacao vai adiantada).
    void sync(uint64_t upTo);
    // Eventos do mesmo mixer que nao sao das portas do adaptador (musica, voz), para quem os espera
    std::vector<MixerEvent> take_other_events();

    bool     busy(uint8_t port) const { return ports_[port].busy; }
    uint32_t tag(uint8_t port) const { return ports_[port].tag; }  // banco*128 + pedido: o ID do jogo
    uint32_t missing() const { return missing_; }
    uint32_t refused() const { return refused_; }
    uint32_t other_overflow() const { return overflow_; }

private:
    struct Port {
        VoiceId  voice = 0;  // a voz que o jogo ve tocando
        VoiceId  tail  = 0;  // ja no fim logico, ainda soando (mod mais longo): nao ocupa o canal
        bool     busy  = false;
        uint32_t tag   = 0;
        int8_t   p15 = 0, p16 = 0;
        uint8_t  baseTl = kSdDefaultBaseTl;  // do som atual (o nivel do volume vale sobre ele)
    };
    uint32_t param(const SdHandle* h, int8_t value, uint16_t time, bool cmd15, bool checkInit);
    void     apply(Port& p);
    void     release(Port& p);
    void     drop(Port& p);  // para as vozes da porta (atual e cauda) e lembra delas para descartar o evento final
    void     other(const MixerEvent& e);

    AudioMixer&              mixer_;
    SdResolver               resolver_;
    SdAdapterOptions         o_;
    bool                     init_ = false;
    std::array<Port, 256>    ports_{};
    std::vector<MixerEvent>  others_;
    std::vector<VoiceId>     dropped_;
    uint32_t                 missing_ = 0, refused_ = 0, overflow_ = 0;
};

} // namespace bs::native::audio
