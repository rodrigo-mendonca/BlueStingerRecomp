#pragma once
// N6 fase F (docs/N6-PLANO.md secao 18.5): o lado do driver Manatee que o jogo comanda pelo SDK de som, em C++ portavel:
// os slots de dados (o que o jogo baixa para a RAM de som), as 8 portas (sequenciador + sintetizador por porta) e os
// faders de volume e pan da porta. Quem traduz as chamadas do SDK e dono do relogio e transitional/sound_adapter; aqui
// so o contrato do driver (MANATEE.DRV, lido no ARM7), sem memoria do guest e sem ARM7/AICA.
//
// Do driver - provado no codigo:
//   - slots (0x14000 SMSB, 0x14080 SMPB; 16 de 8 bytes cada): o download (SDK 8C15D534) grava o arquivo inteiro;
//   - tocar (comando 0x01, 0x4B3C): [porta, slot SMSB, sequencia, x*8]; confere 'SMSB', versao 1 e o rodape 'ENDB' (erro
//     2/8/4 na porta, sem resposta ao SH-4); prepara as trilhas (0x7404) e os 16 canais da porta (0x2758); NAO mexe no
//     volume nem no pan da porta e nao desliga as vozes que ja tocam;
//   - parar (comando 0x02, 0x4C20): desliga (KYONB = 0) todas as vozes da porta e limpa as trilhas (0x74EC);
//   - volume (0x05, 0x4D90) e pan (0x06, 0x4E18): alvo = v + 0x80; com t = 0 o valor vai ao alvo no tick seguinte; senao
//     periodo = t >> 3 e passo = |atual - alvo|, os dois divididos por 2 ate o passo <= 1 ou o periodo <= 1 (o periodo
//     guardado em 8 bits); a cada tick (0x6640/0x66E0): espera o periodo, depois anda `passo` em direcao ao alvo
//     (sem passar). Um comando novo troca alvo, periodo e passo a partir do valor atual (o fade em curso e substituido);
//   - tick de 250 Hz (laco principal 0x1B8): sequenciador (0x4210), depois os faders (0x6780 velocidade, 0x6640 volume,
//     0x66E0 pan); o nivel das vozes segue o volume atual da porta (byte - 0x80).
// Nao usado pelo jogo (sem chamada nas rotas e sem tratador ligado aqui): comandos 0x03/0x04 (0x4CB8/0x4D44), 0x35/0x36.
#include "audio/manatee.h"
#include "audio/manatee_seq.h"
#include "audio/manatee_synth.h"
#include "audio/mixer.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bs::native::audio::manatee {

inline constexpr uint32_t kMagicSmsb = 0x42534D53;  // 'SMSB' (little-endian)
inline constexpr uint32_t kMagicSmpb = 0x42504D53;  // 'SMPB'
inline constexpr size_t   kPorts     = 8;

// Os bytes de fade da porta (0xC000 + 0x80 * porta: +0..+4 volume, +6..+0xA pan)
struct PortFader {
    uint8_t target = 0x80, current = 0x80, period = 0, countdown = 0, step = 0;
    void    command(uint8_t value, uint16_t time);  // 0x4D90 / 0x4E18
    bool    tick();                                 // 0x6640 / 0x66E0; true = o valor mudou
};

enum class PlayError : uint8_t { None = 0, NotSmsb = 2, NoFooter = 4, Version = 8, NoSequence = 16 };

struct PlayerStats {
    uint64_t downloads = 0, plays = 0, stops = 0, volumes = 0, pans = 0, ticks = 0, refusedDownloads = 0;
    uint64_t playErrors = 0;
};

// Uma porta tocando: para o diagnostico e para o adaptador
struct PortView {
    bool     playing = false;
    uint8_t  smsbSlot = 0, sequence = 0;
    uint8_t  volume = 0x80, pan = 0x80;  // os valores atuais dos faders (byte do driver)
    uint64_t calls = 0;
    size_t   sounding = 0;
};

class MusicPlayer {
public:
    MusicPlayer(AudioMixer& mixer, DriverTables drv, Bus bus = Bus::Music);

    // O download de um slot (SMSB ou SMPB). Outro tipo: false (o adaptador manda para o original). Um arquivo que nao
    // le: o slot fica vazio (como uma RAM de som com lixo, nada toca dele) e o motivo vai para `err`.
    bool download(uint32_t magic, uint8_t slot, std::vector<uint8_t> bytes, std::string* err = nullptr);

    PlayError play(uint8_t port, uint8_t smsbSlot, uint8_t sequence, uint8_t x);
    void      stop(uint8_t port);
    void      volume(uint8_t port, int8_t v, uint16_t time);
    void      pan(uint8_t port, int8_t v, uint16_t time);
    void      tick();  // um tick de 250 Hz do driver

    PortView           view(uint8_t port) const;
    const PlayerStats& stats() const { return stats_; }
    SynthStats         synth_stats() const;  // somado nas portas
    // Diagnostico: os eventos de cada porta (porta, evento) e as vozes ligadas (porta, voz)
    std::function<void(uint8_t, const MusicEvent&)> onEvent;
    std::function<void(uint8_t, const VoiceLog&)>   onVoice;

private:
    struct SmsbSlot {
        std::vector<uint8_t>  bytes;
        std::vector<Sequence> seqs;
    };
    struct SmpbSlot {
        std::vector<uint8_t> bytes;
        Bank                 bank;
    };
    struct Port {
        std::shared_ptr<const SmsbSlot> data;  // a sequencia vive enquanto a porta toca, mesmo se o slot trocar
        std::unique_ptr<Sequencer>      seq;
        std::unique_ptr<Synth>          synth;
        PortFader                       vol, pan;
        uint8_t                         smsbSlot = 0, sequence = 0;
        uint64_t                        calls    = 0;
        uint32_t                        nextNoteId = 1;  // continua entre um tocar e outro (ids unicos na porta)
    };

    AudioMixer&                                    mixer_;
    DriverTables                                   drv_;
    Bus                                            bus_;
    std::array<std::shared_ptr<const SmsbSlot>, 16> smsb_;
    std::array<std::shared_ptr<const SmpbSlot>, 16> smpb_;
    std::unique_ptr<ChannelPool>                   pool_ = std::make_unique<ChannelPool>();  // os 48 canais, das 8 portas
    std::array<Port, kPorts>                       ports_;
    std::vector<MusicEvent>                        evbuf_;
    PlayerStats                                    stats_;
};

} // namespace bs::native::audio::manatee
