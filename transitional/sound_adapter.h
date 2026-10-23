#pragma once
// N6 (docs/N6-PLANO.md secoes 10-16): o som do jogo pelo provedor nativo, num mixer so (uma saida, um relogio logico).
//
// Efeitos (fase D, --audio-efeitos nativo): as 5 entradas do SDK de som que os consumidores do gerenciador de efeitos
//   chamam viram o SdAdapter (native/audio/sd_adapter); o gerenciador (fila, prioridade, 7 ativos, rodizio) continua o
//   recompilado. Os efeitos vem do BS_SND.MLT do disco do usuario (native/audio/sosb), decodificados uma vez por ID.
// Falas (fase E, --audio-falas nativo): o MW WAV da CRI (create 8C158FC4 e os metodos da tabela 8C1DD248) vira o
//   VoicePlayer (native/audio/voice_player); o player do jogo (8C08B860, 8C08B900) continua o recompilado. As falas vem
//   dos WAV do disco do usuario (data\disc\BSDATA); dublagens de mods em mods\audio\<idioma>\voz\<nome>.(wav|...)
//   (sound_catalog.h), com o fim que o jogo ve sempre o da fala original.
// Musica (fase F, --audio-musica nativo): as entradas do SDK que o gerenciador de musica do jogo (8C02AD80, 8C02AE20,
//   8C02AE70) e o carregador (8C02A4FC) chamam viram o MusicPlayer (native/audio/manatee_player): download de SMPB/SMSB
//   (8C15D534; os outros tipos seguem para o original), tocar, parar, volume e pan com fade. O gerenciador continua o
//   recompilado; o tick de 250 Hz do driver anda no relogio logico do mixer (176,4 quadros por tick).
// O legado (padrao ate a prova) nao instala nada.
#include "boundary_registry.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace bs::native::audio {
class IAudioOutput;
struct DialogueEvent;
enum class TailPolicy : uint8_t;
} // namespace bs::native::audio

namespace bs::transitional::sound {

struct Options {
    bool                  effects = false;
    std::filesystem::path mlt;  // <data>\disc\BSDATA\BS_SND.MLT
    bool                  voices = false;
    bool                  music  = false;
    std::filesystem::path driver;     // <data>\disc\BSDATA\MANATEE.DRV (as tabelas do driver, conferidas)
    std::filesystem::path voiceDir;   // <data>\disc\BSDATA (os WAV originais)
    std::filesystem::path modsAudio;  // mods\audio (dublagens; opcional)
    std::string           language;   // idioma da dublagem ("" = so o original)
    bool                  cutLongerDubs = false;  // TailPolicy::Cut (padrao PlayOut)
    // eventos de dialogo (legendas: native/localization SubtitleService; o apresentador e do N7)
    std::function<void(const native::audio::DialogueEvent&)> onDialogue;
    // saida: XAudio2 em tempo real; vazio = saida nula (headless: mesmo relogio logico, nada no dispositivo)
    std::function<std::unique_ptr<native::audio::IAudioOutput>()> makeOutput;
    uint32_t rate = 44100;
};

// Lanca std::runtime_error se o executavel nao tem os slots (gere o codigo de novo) ou os dados nao servem.
void install(BoundaryRegistry& registry, Options o);
void uninstall(BoundaryRegistry& registry);
bool installed();
// A cada VBlank (thread do jogo): o tempo logico do mixer avanca 1/59,94 s, as consultas do jogo passam a ver os
// fins ate aqui e a saida recebe o mesmo trecho.
void vblank(uint64_t n);
// O quadro do mixer que se OUVE agora (legendas das falas, 2026-10-23): pelos blocos que a saida aceitou e pela fila atual do
// dispositivo (blocos descartados com a fila cheia nao contam; a reserva de silencio fica antes do primeiro bloco). mixed:
// o quadro misturado ate aqui, devolvido como esta sem saida de verdade (headless) ou sem fila.
uint64_t audible_frame(uint64_t mixed);
std::string summary();  // "[audio] efeitos nativos: ..." e "[audio] falas nativas: ..."
// Pausa do HOST (janela, foco; N7) e troca de cena do host: param ou cortam a fala nativa e a cauda de uma dublagem.
// O jogo original nunca pausa a fala (o metodo +0x28 do MW WAV nao tem chamador) e a troca de cena dele chega como o
// stop da fala; por isso nada do jogo chama estas funcoes (revisao Codex 2026-10-20: fonte no jogo nao comprovada).
void pause(bool on);
void scene_changed();
// N7c (menu interno): volumes do jogador por barramento (0..1, ja com o volume geral), por cima do que o jogo manda.
// Na thread do jogo (o GameHost aplica no VBlank).
void set_bus_volumes(float music, float voices, float effects);

} // namespace bs::transitional::sound
