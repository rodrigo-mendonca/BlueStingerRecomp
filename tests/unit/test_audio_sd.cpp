// N6: adaptador das 5 entradas do SDK de som (native/audio/sd_adapter), so com dados sinteticos e mixer real:
// sem o jogo, sem dispositivo, sem as funcoes recompiladas.
#include "test.h"

#include "audio/mixer.h"
#include "audio/sd_adapter.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace na = bs::native::audio;
namespace er = bs::native::audio::sd_err;

namespace {
std::shared_ptr<na::SoundData> tone(uint32_t frames, int16_t value) {
    auto s  = std::make_shared<na::SoundData>();
    s->rate = 44100;
    s->pcm.assign(frames, value);
    return s;
}
uint32_t word24(const uint8_t* st) {
    uint32_t w;
    std::memcpy(&w, st + 24, 4);
    return w;
}
struct Fixture {
    na::AudioMixer                       mixer{44100, 32};
    std::vector<std::pair<int, int>>     asked;
    uint32_t                             frames = 1000;
    int64_t                              logicalEnd = -1;
    uint8_t                              baseTl = na::kSdDefaultBaseTl;
    na::SdAdapter                        sd;
    explicit Fixture(na::SdAdapterOptions o = {})
        : sd(mixer,
             [this](uint8_t bank, uint8_t req) -> na::SdSound {
                 asked.push_back({bank, req});
                 if (bank == 99) return {};  // banco sem recurso
                 return {tone(frames, 10000), logicalEnd, baseTl};
             },
             std::move(o)) {
        sd.set_initialized(true);
    }
    std::vector<int16_t> render(size_t n) {
        std::vector<int16_t> out(2 * n);
        mixer.render(out);
        return out;
    }
};
} // namespace

// Codigos de retorno como no binario: sem inicializacao (o 0x15 nao confere, como o original), handle nulo,
// saida nula da consulta, 4o argumento do tocar fora de 0..15
BS_TEST(audio_sd_return_codes) {
    Fixture       f;
    na::SdHandle  h{2};
    uint8_t       st[28];
    f.sd.set_initialized(false);
    BS_CHECK(f.sd.stop(&h) == er::kNotInitialized);
    BS_CHECK(f.sd.play(&h, 1, 2, 0) == er::kNotInitialized);
    BS_CHECK(f.sd.param16(&h, 0, 0) == er::kNotInitialized);
    BS_CHECK(f.sd.status(&h, st) == er::kNotInitialized);
    BS_CHECK(f.sd.param15(&h, 0, 0) == er::kOk);          // 8C15E19C: so o handle nulo
    BS_CHECK(f.sd.param15(nullptr, 0, 0) == er::kNullHandle);
    f.sd.set_initialized(true);
    BS_CHECK(f.sd.stop(nullptr) == er::kNullHandle && f.sd.play(nullptr, 1, 2, 0) == er::kNullHandle);
    BS_CHECK(f.sd.param16(nullptr, 0, 0) == er::kNullHandle && f.sd.status(nullptr, st) == er::kNullHandle);
    BS_CHECK(f.sd.status(&h, nullptr) == er::kNullOut);
    BS_CHECK(f.sd.play(&h, 1, 2, -1) == er::kRange && f.sd.play(&h, 1, 2, 16) == er::kRange);
    BS_CHECK(f.asked.empty() && !f.sd.busy(2));             // recusados antes de resolver
    BS_CHECK(f.sd.play(&h, 1, 2, 15) == er::kOk && f.sd.busy(2));
    BS_CHECK(f.sd.param15(&h, 0, 1) == er::kUnsupported && f.sd.param16(&h, 0, 300) == er::kUnsupported);
}

// ID do jogo preservado (banco*128 + pedido); a consulta so libera o canal quando o tempo LOGICO passa do fim
BS_TEST(audio_sd_ids_and_logical_status) {
    Fixture      f;
    na::SdHandle h{3};
    BS_CHECK(f.sd.play(&h, 2, 5, 0) == er::kOk);
    BS_CHECK(f.asked.size() == 1 && f.asked[0] == std::make_pair(2, 5) && f.sd.tag(3) == 2u * 128 + 5);
    uint8_t st[28];
    std::memset(st, 0xAA, sizeof st);
    BS_CHECK(f.sd.status(&h, st) == er::kOk && word24(st) == 1);
    bool zeros = true;
    for (int i = 0; i < 24; ++i) zeros &= st[i] == 0;
    BS_CHECK(zeros);
    f.render(4000);  // a renderizacao passa do fim (1000 quadros)...
    f.sd.status(&h, st);
    BS_CHECK(word24(st) == 1);  // ...mas o jogo ainda nao chegou la
    f.sd.sync(500);
    f.sd.status(&h, st);
    BS_CHECK(word24(st) == 1);
    f.sd.sync(1000);
    f.sd.status(&h, st);
    BS_CHECK(word24(st) == 0 && !f.sd.busy(3) && f.sd.take_other_events().empty());
}

// Efeito com laco (descritor com o bit 1; O6: LPCTL = 1, nao desliga sozinho, +24 = 1 enquanto soa): o som repete
// LSA..LEA, o canal fica ocupado alem do fim fisico e so o parar o libera; o audio continua (nao vira silencio)
BS_TEST(audio_sd_loop_until_stop) {
    na::AudioMixer mixer(44100, 8);
    auto           s = std::make_shared<na::SoundData>();
    s->rate          = 44100;
    s->pcm.assign(1000, 8000);
    s->loopStart = 400, s->loopEnd = 1000;
    na::SdAdapter sd(mixer, [s](uint8_t, uint8_t) { return na::SdSound{s}; });
    sd.set_initialized(true);
    na::SdHandle h{4};
    uint8_t      st[28];
    sd.play(&h, 0, 11, 0);
    std::vector<int16_t> out(2 * 20000);
    mixer.render(out);
    sd.sync(mixer.now());
    BS_CHECK(sd.busy(4) && sd.status(&h, st) == er::kOk && word24(st) == 1);
    BS_CHECK(out[2 * 19990] != 0);  // 20 vezes o som e ainda soando
    sd.stop(&h);
    sd.status(&h, st);
    BS_CHECK(!sd.busy(4) && word24(st) == 0 && mixer.active() == 0);
}

// os 7 canais do gerenciador (rodizio) e o canal avulso; tocar no mesmo canal recomeca sem vazar eventos
BS_TEST(audio_sd_channels_restart) {
    Fixture f;
    for (uint8_t p = 0; p < 8; ++p) {
        na::SdHandle h{p};
        BS_CHECK(f.sd.play(&h, 0, int8_t(p), 0) == er::kOk);
    }
    BS_CHECK(f.mixer.active() == 8);
    for (uint8_t p = 0; p < 8; ++p) BS_CHECK(f.sd.busy(p) && f.sd.tag(p) == p);
    na::SdHandle h0{0};
    f.render(200);
    BS_CHECK(f.sd.play(&h0, 1, 7, 0) == er::kOk);  // o consumidor reaproveita o canal mais antigo
    BS_CHECK(f.mixer.active() == 8 && f.sd.busy(0) && f.sd.tag(0) == 135);
    f.render(5000);
    f.sd.sync(f.mixer.now());
    for (uint8_t p = 0; p < 8; ++p) BS_CHECK(!f.sd.busy(p));
    BS_CHECK(f.sd.take_other_events().empty());  // o Stopped da voz trocada morreu no adaptador
}

// parar (8C15E208) libera na hora; recurso ausente: OK, canal livre, contado; voz recusada pelo mixer
BS_TEST(audio_sd_stop_missing_refused) {
    Fixture      f;
    na::SdHandle h{1};
    f.sd.play(&h, 4, 4, 0);
    BS_CHECK(f.sd.stop(&h) == er::kOk && !f.sd.busy(1) && f.mixer.active() == 0);
    BS_CHECK(f.sd.stop(&h) == er::kOk);  // parar canal livre: nada a fazer
    f.render(100);
    f.sd.sync(f.mixer.now());
    BS_CHECK(f.sd.take_other_events().empty());
    BS_CHECK(f.sd.play(&h, 99, 1, 0) == er::kOk && !f.sd.busy(1) && f.sd.missing() == 1 && f.sd.tag(1) == 99u * 128 + 1);
    // mixer de uma voz: o segundo canal ou e recusado ou rouba o primeiro; nunca os dois ocupados
    na::AudioMixer small(44100, 1);
    na::SdAdapter  sd(small, [](uint8_t, uint8_t) { return na::SdSound{tone(1000, 1)}; });
    sd.set_initialized(true);
    na::SdHandle a{0}, b{1};
    sd.play(&a, 0, 0, 0), sd.play(&b, 0, 1, 0);
    sd.sync(small.now());
    BS_CHECK(int(sd.busy(0)) + int(sd.busy(1)) == 1 && (sd.refused() == 1 || !sd.busy(0)));
}

// Leis MEDIDAS no oraculo O6 (Flycast, valor forcado antes do comando, registradores da AICA no key-on):
// 0x15 = volume (TL), 0x16 = pan (DIPAN). Os 8 pontos medidos, exatos.
BS_TEST(audio_sd_measured_register_laws) {
    BS_CHECK(na::sd_tl(0) == 30 && na::sd_tl(-16) == 62 && na::sd_tl(-64) == 158 && na::sd_tl(63) == 0);
    BS_CHECK(na::sd_dipan(-64) == 31 && na::sd_dipan(-32) == 23 && na::sd_dipan(0) == 0 && na::sd_dipan(16) == 4 &&
             na::sd_dipan(63) == 15);
    BS_CHECK(na::sd_tl(-128) == 255 && na::sd_dipan(-128) == 31 && na::sd_dipan(-1) == 0x10);  // extremos
}

// O nivel base e do EFEITO (descritor SOSP, byte 0x19), nao do driver: O6 com --trocar (2026-10-20) mediu TL 40, 20
// e 45 com volume 0 nos pedidos 11, 67 e 37 do banco 0, igual ao byte. O adaptador aplica a base do som tocado e a
// mantem quando o jogo muda o volume; o proximo som do canal traz a sua.
BS_TEST(audio_sd_effect_base_level) {
    BS_CHECK(na::sd_tl(0, 40) == 40 && na::sd_tl(0, 20) == 20 && na::sd_tl(0, 45) == 45 && na::sd_tl(5, 20) == 10);
    BS_CHECK(na::sd_tl(-64, 200) == 255 && na::sd_tl(63, 20) == 0);  // limites
    Fixture      f;
    na::SdHandle h{3};
    f.baseTl = 20;
    f.sd.play(&h, 0, 67, 0);
    auto o = f.render(10);
    BS_CHECK(std::abs(o[0] - int(10000 * na::sd_volume_gain(0, 20))) <= 1);  // -7,5 dB, nao os -11,25 do 30
    f.sd.param15(&h, -10, 0);  // TL 40
    o = f.render(10);
    BS_CHECK(std::abs(o[0] - int(10000 * std::pow(10.0, -0.375 * 40 / 20))) <= 1);
    f.baseTl = 45;
    f.sd.play(&h, 0, 37, 0);  // o volume -10 do canal continua, sobre a base nova: TL 65
    o = f.render(10);
    BS_CHECK(std::abs(o[0] - int(10000 * std::pow(10.0, -0.375 * 65 / 20))) <= 1);
}

// O adaptador aplica as leis no mixer: papel e conversao vem das opcoes (padrao = o medido); valem na voz atual
// e nas proximas do canal; tempo != 0 e recusado sem mudar nada
BS_TEST(audio_sd_params_roles) {
    Fixture      f;
    na::SdHandle h{5};
    f.sd.play(&h, 0, 1, 0);
    auto o = f.render(10);
    const int base = int(10000 * na::sd_volume_gain(0));  // v = 0: TL 30 (-11,25 dB)
    BS_CHECK(std::abs(o[0] - base) <= 1 && o[0] == o[1]);
    BS_CHECK(f.sd.param15(&h, 63, 0) == er::kOk);  // volume maximo: TL 0
    o = f.render(10);
    BS_CHECK(o[0] == 10000 && o[1] == 10000);
    BS_CHECK(f.sd.param16(&h, -64, 0) == er::kOk);  // pan: DIPAN 31 = so a esquerda
    o = f.render(10);
    BS_CHECK(o[0] == 10000 && o[1] == 0);
    BS_CHECK(f.sd.param16(&h, 16, 0) == er::kOk);  // DIPAN 4: direita, esquerda a -12 dB
    o = f.render(10);
    BS_CHECK(o[1] == 10000 && std::abs(o[0] - int(10000 * std::pow(10.0, -12.0 / 20))) <= 2);
    BS_CHECK(f.sd.param15(&h, 0, 7) == er::kUnsupported);
    o = f.render(10);
    BS_CHECK(o[1] == 10000);  // nada mudou
    f.sd.play(&h, 0, 2, 0);  // os valores do canal valem para a proxima voz
    o = f.render(10);
    BS_CHECK(o[1] == 10000 && o[0] < 10000);
    // papeis trocados pelas opcoes; Ignore nao mexe
    na::SdAdapterOptions sw;
    sw.cmd15 = na::SdParamRole::Ignore;
    sw.cmd16 = na::SdParamRole::Volume;
    Fixture      g(sw);
    na::SdHandle k{0};
    g.sd.play(&k, 0, 0, 0);
    g.sd.param15(&k, -64, 0), g.sd.param16(&k, 63, 0);
    o = g.render(10);
    BS_CHECK(o[0] == 10000 && o[1] == 10000);
}

// eventos de outras vozes do mesmo mixer (musica, fala) passam adiante, no quadro em que aconteceram
BS_TEST(audio_sd_other_events_pass_through) {
    Fixture      f;
    na::SdHandle h{0};
    f.sd.play(&h, 0, 0, 0);
    na::PlayParams pp;
    pp.bus = na::Bus::Music;
    pp.tag = 4242;
    f.mixer.play(tone(300, 1), pp);
    f.render(2000);
    f.sd.sync(f.mixer.now());
    auto ev = f.sd.take_other_events();
    BS_CHECK(ev.size() == 1 && ev[0].tag == 4242 && ev[0].kind == na::EventKind::Finished && ev[0].frame == 300);
    BS_CHECK(!f.sd.busy(0) && f.sd.take_other_events().empty());
}

// Mod mais longo que o original (revisao Codex 2026-10-20, MEDIA): o canal fica livre no fim LOGICO (o
// rodizio e a prioridade do jogo seguem o original); o som continua ate o fim fisico, sem vazar eventos; tocar
// de novo no canal corta o resto
BS_TEST(audio_sd_logical_end_frees_port) {
    Fixture f;
    f.frames     = 3000;
    f.logicalEnd = 1000;
    na::SdHandle h{4};
    f.sd.play(&h, 1, 1, 0);
    f.render(4000);
    f.sd.sync(999);
    BS_CHECK(f.sd.busy(4));
    f.sd.sync(1000);
    uint8_t st[28];
    f.sd.status(&h, st);
    BS_CHECK(!f.sd.busy(4) && word24(st) == 0);
    f.sd.sync(f.mixer.now());  // o Finished fisico (3000) e da cauda: nao vai para os outros
    BS_CHECK(f.sd.take_other_events().empty());
    // de novo, agora com o canal reutilizado antes do fim fisico: a cauda e cortada
    Fixture g;
    g.frames     = 3000;
    g.logicalEnd = 500;
    g.sd.play(&h, 1, 1, 0);
    g.render(600);
    g.sd.sync(g.mixer.now());
    BS_CHECK(!g.sd.busy(4) && g.mixer.active() == 1);  // so a cauda soando
    g.sd.play(&h, 1, 2, 0);
    BS_CHECK(g.sd.busy(4) && g.mixer.active() == 1);  // cauda cortada, a nova voz no lugar
    g.render(4000);
    g.sd.sync(g.mixer.now());
    BS_CHECK(g.sd.take_other_events().empty());
}

// Eventos de outras vozes (revisao Codex 2026-10-20, BAIXA): entregues na hora pelo callback; sem ele, o
// acumulo tem limite e o excedente e contado
BS_TEST(audio_sd_other_events_bounded) {
    auto loop       = tone(100, 1);
    loop->loopStart = 0, loop->loopEnd = 100;
    na::PlayParams pp;
    pp.bus  = na::Bus::Music;
    pp.loop = true;
    pp.tag  = 7;
    {
        Fixture f;
        f.mixer.play(loop, pp);
        for (int i = 0; i < 50; ++i) f.render(1000), f.sd.sync(f.mixer.now());  // ~500 lacos, sem drenar
        BS_CHECK(f.sd.take_other_events().size() <= 1024 && f.sd.other_overflow() == 0);
        na::SdAdapterOptions o;
        o.otherEventsCap = 16;
        Fixture g(o);
        g.mixer.play(loop, pp);
        for (int i = 0; i < 10; ++i) g.render(1000), g.sd.sync(g.mixer.now());
        BS_CHECK(g.sd.take_other_events().size() == 16 && g.sd.other_overflow() > 0);
    }
    size_t               delivered = 0;
    na::SdAdapterOptions o;
    o.onOtherEvent = [&](const na::MixerEvent& e) { delivered += e.tag == 7; };
    Fixture h(o);
    h.mixer.play(loop, pp);
    for (int i = 0; i < 10; ++i) h.render(1000), h.sd.sync(h.mixer.now());
    BS_CHECK(delivered >= 99 && h.sd.take_other_events().empty() && h.sd.other_overflow() == 0);
}
