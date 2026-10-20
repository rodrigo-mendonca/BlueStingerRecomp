#pragma once
// N6 fase E (docs/N6-PLANO.md secao 16): a cola entre o guest e o player nativo das falas (native/audio/voice_player),
// sem Sh4Context nem runtime. Os slots (sound_adapter.cpp) so traduzem registradores e memoria; os testes usam uma RAM
// sintetica.
//
// Contrato com o guest (disassembly de 2026-10-20, docs/N6-PLANO.md secao 16):
//   create 8C158FC4(r4 = parametros): pool estatico do MW WAV, 32 entradas de 56 B a partir de 0x8C2D7D38 (o literal
//     0x8C2D7D30 + 8, constante no codigo); a primeira com +4 == 0 e usada: +0 = tabela 8C1DD248, +4 = 1 (em uso).
//     Devolve o endereco, ou 0 com o pool cheio (o original tambem). So uma fala nativa vive por vez (o jogo guarda um
//     handle em S+4 e so cria outro depois do destroy); um segundo create com o primeiro vivo falha e fica contado.
//   metodos da tabela 8C1DD248, so para o handle criado aqui (outro handle segue para o original):
//     +0x14 destroy(h)  +0x18 start(h, nome)  +0x1C stop(h)  +0x20 estado(h)  +0x28 (h, modo): 0/1 (sem chamador
//     no jogo; aceito e contado)  +0x0C/+0x10 servidor por handle e +0x24/+0x2C/+0x30/+0x34: sem uso pelo jogo
//     com o handle nativo; respondem 0 e ficam contados (tem que ficar 0: prova de que nada do original o ve).
//   Nada do guest aponta para memoria do host; o player vive entre start e stop/destroy.
#include "audio/voice_player.h"

#include <cstdint>
#include <optional>
#include <string>

namespace bs::transitional::sound {

constexpr uint32_t kWavCreate = 0x8C158FC4, kWavServer0C = 0x8C1597C0, kWavServer10 = 0x8C159738, kWavDestroy = 0x8C159134,
                   kWavStart = 0x8C1591C8, kWavStop = 0x8C1592A0, kWavStat = 0x8C159340, kWav24 = 0x8C159394,
                   kWavMode = 0x8C1593CA, kWav2C = 0x8C15941C, kWav30 = 0x8C1594C4, kWav34 = 0x8C15950E;
constexpr uint32_t kWavPool = 0x8C2D7D38, kWavVtable = 0x8C1DD248, kWavEntries = 32, kWavEntrySize = 56;

class VoiceGuest {
public:
    virtual ~VoiceGuest()                                                 = default;
    virtual std::optional<uint32_t>    load32(uint32_t addr)              = 0;
    virtual bool                       store32(uint32_t addr, uint32_t v) = 0;
    virtual std::optional<std::string> cstring(uint32_t addr, size_t max) = 0;
};

struct VoiceOutcome {
    bool     forward = false;  // handle de outro player: chamar o original
    uint32_t r0      = 0;
};

struct VoiceGlueStats {
    uint64_t creates = 0, createFailures = 0, destroys = 0, foreign = 0, unexpected = 0, modeCalls = 0;
};

class VoiceGlue {
public:
    explicit VoiceGlue(native::audio::VoicePlayer& player) : player_(player) {}
    VoiceOutcome call(uint32_t entry, uint32_t r4, uint32_t r5, VoiceGuest& g);
    uint32_t              handle() const { return handle_; }
    const VoiceGlueStats& stats() const { return stats_; }

private:
    VoiceOutcome                create(VoiceGuest& g);
    native::audio::VoicePlayer& player_;
    uint32_t                    handle_ = 0;
    VoiceGlueStats              stats_;
};

} // namespace bs::transitional::sound
