// N6 fase E: player das falas (native/audio/voice_player), so com dados sinteticos e o mixer real: os estados que o
// jogo ve (contrato do MW WAV no binario: 1 preparando, 2 tocando, 3 fim, 4 erro), os eventos de dialogo para as
// legendas e as politicas de dublagem (fim logico x fim fisico).
#include "test.h"

#include "audio/mixer.h"
#include "audio/voice_player.h"

#include <memory>
#include <string>
#include <vector>

namespace na = bs::native::audio;

namespace {
std::shared_ptr<na::SoundData> tone(uint32_t frames) {
    auto s  = std::make_shared<na::SoundData>();
    s->rate = 44100;
    s->pcm.assign(frames, 9000);
    return s;
}
struct Fixture {
    na::AudioMixer                  mixer{44100, 16};
    std::vector<na::DialogueEvent>  ev;
    uint32_t                        frames = 1000;
    int64_t                         logicalEnd = -1;
    na::VoicePlayer                 vp;
    explicit Fixture(na::VoicePlayerOptions o = {})
        : vp(mixer,
             [this](std::string_view n) {
                 na::VoiceSource s;
                 if (n == "E999.WAV") return s;  // ausente
                 s.data       = tone(frames);
                 s.logicalEnd = logicalEnd;
                 s.id         = "voz/" + std::string(n.substr(0, n.find('.')));
                 return s;
             },
             with(std::move(o))) {}
    na::VoicePlayerOptions with(na::VoicePlayerOptions o) {
        o.prepSteps = 1;  // um passo de preparo nos ciclos abaixo; os padroes medidos tem teste proprio
        o.endSteps  = 0;  // o 3 no fim do audio (o atraso medido de 2 passos tem teste proprio)
        o.onEvent = [this](const na::DialogueEvent& e) {  // o pedido (diagnostico das legendas) e contado a parte
            if (e.kind == na::DialogueKind::Requested) ++requested, requestFrame = e.frame;
            else ev.push_back(e);
        };
        return o;
    }
    // o tempo logico anda n quadros (o que o host faz por VBlank) e os eventos chegam ao player
    void advance(size_t n) {
        std::vector<int16_t> out(2 * n);
        mixer.render(out);
        for (const auto& e : mixer.take_events(mixer.now())) vp.on_mixer_event(e);
    }
    int      requested    = 0;
    uint64_t requestFrame = 0;
    std::vector<na::DialogueKind> kinds() const {
        std::vector<na::DialogueKind> k;
        for (const auto& e : ev) k.push_back(e.kind);
        return k;
    }
};
using K = na::DialogueKind;
} // namespace

// fala original: "preparando" (1) ate o passo de jogo seguinte (o audio ainda nao sai), depois 2 ate o fim do audio,
// entao 3; o jogo chama stop. Eventos: Started quando o preparo acaba, LogicalEnd e PhysicalEnd juntos no fim
BS_TEST(audio_voice_original_cycle) {
    Fixture f;
    BS_CHECK(f.vp.start("E001A.WAV") == na::kVoiceStop && f.vp.current() == "voz/E001A");
    f.advance(300);
    BS_CHECK(f.vp.status() == na::kVoicePrep && f.ev.empty() && f.mixer.active() == 1);  // preparando: nada soou
    BS_CHECK(f.requested == 1 && f.requestFrame == 0);  // o pedido chega no start, antes do preparo
    f.vp.game_step();
    BS_CHECK(f.vp.status() == na::kVoicePlaying && f.kinds() == std::vector<K>{K::Started});
    const uint64_t t0 = f.mixer.now();
    f.advance(999);
    BS_CHECK(f.vp.status() == na::kVoicePlaying);
    f.advance(1);
    BS_CHECK(f.vp.status() == na::kVoiceEnd);
    BS_CHECK((f.kinds() == std::vector<K>{K::Started, K::LogicalEnd, K::PhysicalEnd}) && f.ev[1].frame == t0 + 1000);
    f.vp.stop();
    BS_CHECK(f.vp.state() == na::kVoiceStop && !f.vp.sounding() && f.ev.size() == 3);
    BS_CHECK(f.vp.stats().starts == 1 && f.vp.stats().logicalEnds == 1 && f.vp.stats().interrupted == 0);
}

// recurso ausente: o jogo ve o erro (4) e encerra a fala como no original; evento Missing para o log/legendas
BS_TEST(audio_voice_missing) {
    Fixture f;
    f.vp.start("E999.WAV");
    BS_CHECK(f.vp.status() == na::kVoiceError && f.kinds() == std::vector<K>{K::Missing} && f.vp.stats().missing == 1);
    BS_CHECK(f.mixer.active() == 0);
}

// stop antes do fim: interrupcao, o audio para junto
BS_TEST(audio_voice_interrupted) {
    Fixture f;
    f.vp.start("E002A.WAV");
    f.vp.game_step();  // preparo
    f.advance(400);
    f.vp.stop();
    BS_CHECK((f.kinds() == std::vector<K>{K::Started, K::Interrupted}) && f.mixer.active() == 0 && f.vp.stats().interrupted == 1);
}

// dublagem mais longa (3000 quadros, fim logico na duracao original 1000): o jogo ve o 3 em 1000; com PlayOut o stop
// do jogo deixa a cauda soar ate o fim fisico; a fala seguinte corta a cauda. O tempo do jogo nao muda.
BS_TEST(audio_voice_longer_dub_playout) {
    Fixture f;
    f.frames = 3000, f.logicalEnd = 1000;
    f.vp.start("E003.WAV");
    f.vp.game_step();  // preparo
    f.advance(1000);
    BS_CHECK(f.vp.status() == na::kVoiceEnd);
    f.vp.stop();
    BS_CHECK(f.vp.sounding() && f.mixer.active() == 1);  // a cauda continua
    f.advance(2000);
    BS_CHECK(!f.vp.sounding() && (f.kinds() == std::vector<K>{K::Started, K::LogicalEnd, K::PhysicalEnd}));
    // de novo, agora cortada pela fala seguinte
    f.ev.clear();
    f.vp.start("E004A.WAV");
    f.vp.game_step();  // preparo
    f.advance(1000);
    f.vp.stop();
    BS_CHECK(f.vp.sounding());
    f.vp.start("E004B.WAV");
    BS_CHECK(f.vp.stats().tailsCut == 1 && f.mixer.active() == 1);
    BS_CHECK((f.kinds() == std::vector<K>{K::Started, K::LogicalEnd, K::Interrupted}));
}

// politica Cut: o stop do jogo no fim logico corta o audio; a troca de cena corta a cauda na politica PlayOut
BS_TEST(audio_voice_longer_dub_cut_and_scene) {
    na::VoicePlayerOptions o;
    o.tail = na::TailPolicy::Cut;
    Fixture c(o);
    c.frames = 3000, c.logicalEnd = 1000;
    c.vp.start("E005.WAV");
    c.vp.game_step();
    c.advance(1000);
    c.vp.stop();
    BS_CHECK(!c.vp.sounding() && c.mixer.active() == 0 && c.vp.stats().tailsCut == 1);
    Fixture s;
    s.frames = 3000, s.logicalEnd = 1000;
    s.vp.start("E006A.WAV");
    s.vp.game_step();
    s.advance(1000);
    s.vp.stop();
    s.vp.scene_changed();
    BS_CHECK(!s.vp.sounding() && s.vp.stats().tailsCut == 1 && s.kinds().back() == K::Interrupted);
}

// pausa do jogo: o audio e o fim logico param; a fala termina depois, pela duracao dela, sem pular nada
BS_TEST(audio_voice_pause) {
    Fixture f;
    f.vp.start("E007A.WAV");
    f.vp.game_step();  // preparo
    f.advance(500);
    f.vp.pause(true);
    f.advance(5000);
    BS_CHECK(f.vp.status() == na::kVoicePlaying);
    f.vp.pause(false);
    f.advance(499);
    BS_CHECK(f.vp.status() == na::kVoicePlaying);
    f.advance(1);
    BS_CHECK(f.vp.status() == na::kVoiceEnd);
    BS_CHECK((f.kinds() == std::vector<K>{K::Started, K::Paused, K::Resumed, K::LogicalEnd, K::PhysicalEnd}));
}

// padrao MEDIDO (Flycast com o TMU e legado, 2026-10-20): 3 passos de jogo em "preparando"; a consulta so le (revisao
// Codex): varias no mesmo passo devolvem o mesmo e nao adiantam o preparo
BS_TEST(audio_voice_default_prep_steps) {
    na::AudioMixer  mixer(44100, 4);
    na::VoicePlayer vp(mixer, [](std::string_view) {
        na::VoiceSource s;
        auto            d = std::make_shared<na::SoundData>();
        d->rate           = 44100;
        d->pcm.assign(100, 1);
        s.data = d, s.id = "voz/e012aenv";
        return s;
    });
    vp.start("E012AENV.WAV");  // passo 1039 do combate
    for (int i = 0; i < 10; ++i) BS_CHECK(vp.status() == na::kVoicePrep);  // consultas repetidas: nada muda
    for (int i = 0; i < 2; ++i) {  // passos 1040 e 1041
        vp.game_step();
        BS_CHECK(vp.status() == na::kVoicePrep && vp.status() == na::kVoicePrep);
    }
    vp.game_step();  // passo 1042: tocando (o legado e o Flycast tambem)
    BS_CHECK(vp.status() == na::kVoicePlaying && vp.status() == na::kVoicePlaying);
}

// padrao MEDIDO (Flycast com o TMU, 2026-10-20): o estado 3 chega 2 passos de jogo depois do fim do audio (o servidor do
// MW percebe o fim por passo); o fim fisico (legendas) e no fim do audio; a pausa segura a contagem
BS_TEST(audio_voice_default_end_steps) {
    na::AudioMixer                 mixer(44100, 4);
    std::vector<na::DialogueKind>  ev;
    na::VoicePlayerOptions         o;
    o.onEvent = [&](const na::DialogueEvent& e) {
        if (e.kind != na::DialogueKind::Requested) ev.push_back(e.kind);
    };
    na::VoicePlayer vp(mixer, [](std::string_view) {
        na::VoiceSource s;
        auto            d = std::make_shared<na::SoundData>();
        d->rate           = 44100;
        d->pcm.assign(1000, 1);
        s.data = d, s.id = "voz/e002a";
        return s;
    }, o);
    vp.start("E002A.WAV");
    for (int i = 0; i < 3; ++i) vp.game_step();
    BS_CHECK(vp.status() == na::kVoicePlaying);
    std::vector<int16_t> buf(2 * 1000);
    mixer.render(buf);
    for (const auto& e : mixer.take_events(mixer.now())) vp.on_mixer_event(e);
    BS_CHECK(vp.status() == na::kVoicePlaying && ev.back() == na::DialogueKind::PhysicalEnd);  // o audio acabou
    vp.game_step();
    BS_CHECK(vp.status() == na::kVoicePlaying);
    vp.pause(true);
    vp.game_step();  // pausado: nao conta
    BS_CHECK(vp.status() == na::kVoicePlaying);
    vp.pause(false);
    vp.game_step();
    BS_CHECK(vp.status() == na::kVoiceEnd && ev.back() == na::DialogueKind::LogicalEnd);
}
