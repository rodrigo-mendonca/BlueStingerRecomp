#pragma once
// N6 fase E (docs/LOCALIZACAO-PLANO.md secao 4): liga os eventos de fala do player nativo (native/audio/voice_player.h)
// ao SubtitleService. Nada daqui desenha (o apresentador e do N7) nem muda o jogo: so traduz eventos e tempo.
//
// Sincronia (decidida aqui, para dublagens): a legenda acompanha o AUDIO da fala, nao o fim que o jogo ve.
//   Started     -> dialogue_started (o instante em que o jogo ve a fala tocando; tempo logico do mixer)
//   LogicalEnd  -> nada (numa dublagem mais longa o audio continua; a legenda tambem)
//   PhysicalEnd -> dialogue_ended
//   Interrupted -> dialogue_interrupted (stop antes do fim, cauda cortada pela fala seguinte ou pela cena)
//   Paused/Resumed -> paused(on, t)
//   Missing     -> nada (sem fala, sem legenda)
// A cena vem de quem chama (contexto do jogo, quando conhecido); "" = a legenda geral da fala.
#include "audio/voice_player.h"
#include "localization/subtitle_service.h"

#include <cstdint>
#include <string>

namespace bs::native::localization {

class DialogueBridge {
public:
    DialogueBridge(SubtitleService& subs, uint32_t mixerRate) : subs_(subs), rate_(mixerRate) {}
    void on_event(const audio::DialogueEvent& e);
    void set_scene(std::string scene) { scene_ = std::move(scene); }
    void logical_frame(uint64_t frame) { subs_.logical_time(to100ns(frame)); }  // a cada VBlank

private:
    int64_t          to100ns(uint64_t frame) const { return rate_ ? int64_t(frame * 10000000ull / rate_) : 0; }
    SubtitleService& subs_;
    uint32_t         rate_;
    std::string      scene_;
};

} // namespace bs::native::localization
