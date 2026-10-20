#pragma once
// N6 fase E (docs/N6-PLANO.md secao 16): o player das falas do jogo, portavel (sem o contexto da CPU nem o guest).
//
// O jogo (8C08B860 e a atualizacao por quadro 8C08B900, que continuam os originais recompilados) usa o MW WAV da CRI
// (PlyCreateWav 8C158FC4, metodos na tabela 8C1DD248) assim, confirmado no binario:
//   start(nome) -> a cada quadro estado(): 2 = tocando (o contador de inicio S+58 vira -1), 3 = fim ou 4 = erro
//   (o jogo chama stop, destroy, limpa a bandeira S+56 que bloqueia a fala seguinte e devolve o volume da musica
//   com 8C02AE70(0, 800)). Enquanto o estado nao chega a 2, S+58 conta de 15 para 0; dois trechos do gameplay
//   (8C052BE8, 8C083746) testam S+58 != 0. Entao o que o jogo ve e: o passo do start, quantas consultas ficam em 1
//   (preparando) e o passo do estado 3. Isso e o que este player reproduz, no tempo LOGICO do mixer.
//
// Dublagem (VoiceSource::logicalEnd, sound_catalog.h): o estado 3 chega no fim LOGICO (a duracao original) e nunca
// depois; o audio de um arquivo mais longo continua conforme TailPolicy. O relogio do jogo nunca muda.
#include "audio/mixer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace bs::native::audio {

enum VoiceStat : int32_t { kVoiceStop = 0, kVoicePrep = 1, kVoicePlaying = 2, kVoiceEnd = 3, kVoiceError = 4 };

struct VoiceSource {
    std::shared_ptr<const SoundData> data;  // nulo = recurso ausente
    int64_t     logicalEnd = -1;            // fim que o jogo ve, em quadros da fonte (-1 = o fim fisico)
    std::string id;                         // ID estavel: "voz/E001A" (sound_catalog.h)
    bool        mod = false;
    std::string lang;                       // idioma do mod ("" = original)
};
// nome como o jogo passa ao start ("E001A.WAV"); devolve o recurso ja resolvido (mod > original)
using VoiceResolver = std::function<VoiceSource(std::string_view gameName)>;

// O que acontece com o audio de uma dublagem mais longa depois do fim logico (o jogo ja seguiu adiante e chama stop):
//   PlayOut: continua ate acabar; corta so com a fala seguinte, a troca de cena ou a pausa longa do host (padrao)
//   Cut:     o stop do jogo corta (o audio nunca passa da duracao original)
enum class TailPolicy : uint8_t { PlayOut, Cut };

enum class DialogueKind : uint8_t {
    Started,      // a fala comecou a soar (estado 2)
    LogicalEnd,   // o fim que o jogo ve (estado 3, endSteps passos depois do fim do audio original)
    PhysicalEnd,  // o audio acabou (igual ao LogicalEnd no original; depois dele numa dublagem mais longa)
    Interrupted,  // o jogo parou antes do fim logico, ou a cauda foi cortada (fala seguinte, cena)
    Missing,      // start sem recurso: o jogo recebe o estado de erro (4)
    Paused,
    Resumed,
};
struct DialogueEvent {
    DialogueKind kind;
    std::string  id;     // "voz/E001A"
    std::string  name;   // nome original ("E001A.WAV")
    uint64_t     frame;  // quadro LOGICO do mixer (tempo do jogo)
};

struct VoicePlayerOptions {
    // passos de jogo em "preparando" (1) antes do 2. No original o servidor do MW avanca o estado uma vez por passo;
    // a consulta so le (8C159340: valida o handle e devolve a palavra; revisao Codex 2026-10-20). MEDIDO (2026-10-20,
    // Flycast com o TMU): 3 passos nas 3 falas medidas (E012AENV, E012BENV, E002A), 0,251 s e 0,123 s conforme a
    // duracao do passo; o legado tambem 3. O jogo ve a diferenca: o contador S+58 (15, 14, 13, -1) e lido pelo
    // gameplay. 0 = toca no start.
    int        prepSteps = 3;
    // passos de jogo entre o fim do audio (ou o fim logico de uma dublagem) e o estado 3. MEDIDO (2026-10-20, Flycast
    // com o TMU): 1,8, 2,2 e 2,3 passos em E002A, E012BENV e E012AENV (0,063 s, 0,25 s e 0,31 s, conforme a duracao do
    // passo): o servidor do MW percebe o fim por passo, como no preparo. Resolucao: um passo.
    int        endSteps = 2;
    int32_t    missingStat = kVoiceError;
    TailPolicy tail        = TailPolicy::PlayOut;
    float      volume      = 1.0f;
    std::function<void(const DialogueEvent&)> onEvent;
};

struct VoicePlayerStats {
    uint64_t starts = 0, missing = 0, statusCalls = 0, logicalEnds = 0, interrupted = 0, tailsCut = 0;
};

class VoicePlayer {
public:
    VoicePlayer(AudioMixer& mixer, VoiceResolver resolver, VoicePlayerOptions o = {});

    int32_t start(std::string_view gameName);  // metodo +0x18
    int32_t status();                           // metodo +0x20: uma consulta do jogo (so le; nada muda)
    // Um passo de jogo terminou (o host avisa uma vez por passo): o preparo anda aqui, nunca na consulta
    void    game_step();
    void    stop();                             // metodo +0x1C
    void    pause(bool on);                     // pausa do jogo (o tempo logico da fala para)
    void    scene_changed();                    // corta a cauda de uma dublagem (troca de cena)
    // eventos do mixer (o dono do mixer repassa os que nao sao dele; os de outras vozes sao ignorados)
    void    on_mixer_event(const MixerEvent& e);

    int32_t                 state() const { return state_; }
    const std::string&      current() const { return id_; }
    bool                    sounding() const;  // a fala ou a cauda ainda soam
    const VoicePlayerStats& stats() const { return stats_; }

private:
    void emit(DialogueKind k);
    void cutTail();
    void audioEnded();
    void reachEnd();

    AudioMixer&        mixer_;
    VoiceResolver      resolver_;
    VoicePlayerOptions o_;
    VoiceId            voice_ = 0;  // a fala atual (o jogo a ve)
    VoiceId            tail_  = 0;  // dublagem depois do fim logico: soa, o jogo nao ve
    int32_t            state_ = kVoiceStop;
    int                prepLeft_ = 0;  // passos de preparo que faltam (a pausa os segura)
    int                endLeft_  = -1; // passos ate o estado 3 depois do fim (-1: o fim ainda nao veio)
    bool               logicalSeen_ = false, physicalSeen_ = false, paused_ = false;
    std::string        id_, name_;
    VoicePlayerStats   stats_;
};

} // namespace bs::native::audio
