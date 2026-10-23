#include "audio/voice_player.h"

#include <utility>

namespace bs::native::audio {

VoicePlayer::VoicePlayer(AudioMixer& mixer, VoiceResolver resolver, VoicePlayerOptions o)
    : mixer_(mixer), resolver_(std::move(resolver)), o_(std::move(o)) {}

void VoicePlayer::emit(DialogueKind k) {
    if (o_.onEvent) o_.onEvent(DialogueEvent{k, id_, name_, mixer_.now()});
}

bool VoicePlayer::sounding() const { return (voice_ && mixer_.playing(voice_)) || (tail_ && mixer_.playing(tail_)); }

void VoicePlayer::cutTail() {
    if (!tail_) return;
    if (mixer_.playing(tail_)) {
        mixer_.stop(tail_);
        ++stats_.tailsCut;
        emit(DialogueKind::Interrupted);
    }
    tail_ = 0;
}

int32_t VoicePlayer::start(std::string_view gameName) {
    cutTail();  // uma fala nunca sobrepoe a cauda da anterior
    if (voice_) mixer_.stop(voice_), voice_ = 0;
    ++stats_.starts;
    name_.assign(gameName);
    VoiceSource s = resolver_ ? resolver_(gameName) : VoiceSource{};
    id_           = s.id.empty() ? name_ : s.id;
    logicalSeen_ = physicalSeen_ = false;
    endLeft_     = -1;
    emit(DialogueKind::Requested);
    if (!s.data) {
        ++stats_.missing;
        state_ = o_.missingStat;
        emit(DialogueKind::Missing);
        return kVoiceStop;
    }
    PlayParams pp;
    pp.bus        = Bus::Voice;
    pp.volume     = o_.volume;
    pp.priority   = 100;  // a fala nunca e roubada por um efeito
    pp.logicalEnd = s.logicalEnd;
    voice_        = mixer_.play(std::move(s.data), pp);
    if (!voice_) {
        state_ = kVoiceError;
        emit(DialogueKind::Missing);
        return kVoiceStop;
    }
    prepLeft_ = o_.prepSteps;
    state_    = prepLeft_ > 0 ? kVoicePrep : kVoicePlaying;
    // preparando: a voz espera parada (no original o audio so sai depois da leitura do disco)
    if (paused_ || state_ == kVoicePrep) mixer_.pause(voice_, true);
    if (state_ == kVoicePlaying) emit(DialogueKind::Started);
    return kVoiceStop;
}

int32_t VoicePlayer::status() {
    ++stats_.statusCalls;
    return state_;
}

void VoicePlayer::reachEnd() {
    state_ = kVoiceEnd;
    ++stats_.logicalEnds;
    emit(DialogueKind::LogicalEnd);
}

void VoicePlayer::game_step() {
    if (endLeft_ > 0 && !paused_ && --endLeft_ == 0 && state_ == kVoicePlaying) return reachEnd();
    if (state_ != kVoicePrep || paused_ || --prepLeft_ > 0) return;
    state_ = kVoicePlaying;  // o audio comeca agora; o jogo ve o 2 na proxima consulta
    mixer_.pause(voice_, false);
    emit(DialogueKind::Started);
}

void VoicePlayer::stop() {
    if (!voice_) {
        if (o_.tail == TailPolicy::Cut) cutTail();
        state_ = kVoiceStop;
        return;
    }
    if (!logicalSeen_) {  // antes do fim que o jogo ve: interrupcao (o audio para junto)
        mixer_.stop(voice_);
        ++stats_.interrupted;
        emit(DialogueKind::Interrupted);
    } else if (!physicalSeen_ && mixer_.playing(voice_)) {  // dublagem mais longa: a cauda
        if (o_.tail == TailPolicy::Cut) {
            mixer_.stop(voice_);
            ++stats_.tailsCut;
            emit(DialogueKind::Interrupted);
        } else {
            tail_ = voice_;
        }
    }
    voice_ = 0;
    state_ = kVoiceStop;
}

void VoicePlayer::pause(bool on) {
    if (on == paused_) return;
    paused_ = on;
    if (voice_ && state_ != kVoicePrep) mixer_.pause(voice_, on);
    if (tail_) mixer_.pause(tail_, on);
    if (voice_ || tail_) emit(on ? DialogueKind::Paused : DialogueKind::Resumed);
}

void VoicePlayer::scene_changed() { cutTail(); }

// o audio (ou a duracao original) acabou: o jogo ve o 3 endSteps passos depois, como o servidor do MW original
void VoicePlayer::audioEnded() {
    if (o_.endSteps <= 0) return reachEnd();
    endLeft_ = o_.endSteps;
}

void VoicePlayer::on_mixer_event(const MixerEvent& e) {
    if (e.voice == tail_ && tail_) {
        if (e.kind == EventKind::Finished) emit(DialogueKind::PhysicalEnd), tail_ = 0;
        else if (e.kind == EventKind::Stopped || e.kind == EventKind::Stolen) tail_ = 0;
        return;
    }
    if (e.voice != voice_ || !voice_) return;
    switch (e.kind) {
    case EventKind::LogicalEnd:  // so com logicalEnd < fim fisico (dublagem mais longa)
        if (!logicalSeen_) logicalSeen_ = true, audioEnded();
        break;
    case EventKind::Finished:
        if (!logicalSeen_) logicalSeen_ = true, audioEnded();
        physicalSeen_ = true;
        emit(DialogueKind::PhysicalEnd);
        break;
    case EventKind::Stolen:  // nao deveria acontecer (prioridade 100): o jogo ve o erro
        state_ = kVoiceError;
        voice_ = 0;
        emit(DialogueKind::Interrupted);
        break;
    default:
        break;
    }
}

} // namespace bs::native::audio
