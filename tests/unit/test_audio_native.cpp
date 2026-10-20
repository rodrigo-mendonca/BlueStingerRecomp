// N6: audio de gameplay portavel (native/audio), so com dados sinteticos: sem o jogo, sem dispositivo.
#include "test.h"

#include "audio/aica_adpcm.h"
#include "audio/mixer.h"
#include "audio/sound_catalog.h"
#include "audio/sound_file.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace na = bs::native::audio;

namespace {
struct Dir {
    fs::path p = fs::temp_directory_path() / ("bs-audio-" + std::to_string(std::random_device{}()));
    Dir() { fs::create_directories(p); }
    ~Dir() { std::error_code ec; fs::remove_all(p, ec); }
};
std::vector<uint8_t> wavBytes(uint16_t tag, uint16_t ch, uint32_t rate, uint16_t bits, const std::vector<uint8_t>& data,
                              int64_t loopStart = -1, int64_t loopEnd = -1) {
    std::vector<uint8_t> b;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); };
    auto put16 = [&](uint16_t v) { b.push_back(uint8_t(v)), b.push_back(uint8_t(v >> 8)); };
    auto tagS  = [&](const char* s) { b.insert(b.end(), s, s + 4); };
    tagS("RIFF"), put32(0), tagS("WAVE");
    tagS("fmt "), put32(16), put16(tag), put16(ch), put32(rate), put32(rate * ch * bits / 8), put16(uint16_t(ch * bits / 8)), put16(bits);
    if (loopStart >= 0) {
        tagS("smpl"), put32(60);
        for (int i = 0; i < 7; ++i) put32(0);
        put32(1), put32(0);
        put32(0), put32(0), put32(uint32_t(loopStart)), put32(uint32_t(loopEnd - 1)), put32(0), put32(0);
    }
    tagS("data"), put32(uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    if (data.size() & 1) b.push_back(0);
    return b;
}
std::shared_ptr<na::SoundData> tone(uint32_t frames, int16_t value, uint32_t rate = 44100) {
    auto s  = std::make_shared<na::SoundData>();
    s->rate = rate;
    s->pcm.assign(frames, value);
    return s;
}
} // namespace

// ADPCM da AICA: limites do passo e do sinal; ida e volta de uma senoide com boa relacao sinal/ruido
BS_TEST(audio_aica_adpcm_roundtrip) {
    std::vector<int16_t>  sine(22050);
    for (size_t i = 0; i < sine.size(); ++i) sine[i] = int16_t(12000 * std::sin(2 * 3.14159265 * 440 * i / 22050.0));
    na::AicaAdpcmState enc;
    const auto         adpcm = na::aica_adpcm_encode(sine, enc);
    BS_CHECK(adpcm.size() == sine.size() / 2);
    na::AicaAdpcmState   dec;
    std::vector<int16_t> out;
    na::aica_adpcm_decode(adpcm, out, dec);
    BS_CHECK(out.size() == sine.size());
    double sig = 0, err = 0;
    for (size_t i = 0; i < out.size(); ++i) sig += double(sine[i]) * sine[i], err += double(sine[i] - out[i]) * (sine[i] - out[i]);
    BS_CHECK(10 * std::log10(sig / err) > 20.0);
    // nibbles extremos repetidos: satura sem estourar
    std::vector<uint8_t> up(2000, 0x77);  // +15 repetido
    na::AicaAdpcmState   s2;
    std::vector<int16_t> o2;
    na::aica_adpcm_decode(up, o2, s2);
    BS_CHECK(o2.back() == 32767 && s2.step == 24576);
}

// WAV: PCM 16, ADPCM da AICA (o formato original), laco pelo smpl, formatos sem suporte com erro claro
BS_TEST(audio_wav_loading) {
    std::vector<uint8_t> pcm16(400);
    for (size_t i = 0; i < 200; ++i) pcm16[i * 2] = uint8_t(i), pcm16[i * 2 + 1] = 0;
    auto s = na::parse_wav(wavBytes(1, 1, 22050, 16, pcm16, 50, 150));
    BS_CHECK(s && s->frames() == 200 && s->rate == 22050 && s->loopStart == 50 && s->loopEnd == 150 && s->pcm[10] == 10);
    na::AicaAdpcmState enc;
    std::vector<int16_t> ramp(1000);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = int16_t(i * 10);
    auto a = na::parse_wav(wavBytes(0, 1, 22050, 4, na::aica_adpcm_encode(ramp, enc)));
    BS_CHECK(a && a->frames() == 1000 && std::abs(a->pcm[999] - 9990) < 600);
    std::string err;
    BS_CHECK(!na::parse_wav(wavBytes(2, 1, 22050, 4, {1, 2, 3}), &err) && err.find("sem suporte") != std::string::npos);
    BS_CHECK(!na::parse_wav(std::vector<uint8_t>{'R', 'I', 'F', 'F'}, &err));
    Dir d;
    std::ofstream(d.p / "x.flac") << "fLaC";
    BS_CHECK(!na::load_sound(d.p / "x.flac", &err) && err.find("flac") != std::string::npos);
}

// mixer: fim no quadro certo (com reamostragem), eventos, volume por barramento, pan
BS_TEST(audio_mixer_finish_volume_pan) {
    na::AudioMixer m(44100, 8);
    const auto     v = m.play(tone(22050, 10000, 22050), {na::Bus::Voice, 1.0f, 0.0f, 0, false, 77});  // 1 s a 22 kHz
    std::vector<int16_t> out(2 * 44100);
    m.render(out);
    BS_CHECK(m.playing(v) == false || out[0] == 10000);
    BS_CHECK(out[0] == 10000 && out[1] == 10000);
    auto ev = m.take_events();
    BS_CHECK(ev.size() == 1 && ev[0].kind == na::EventKind::Finished && ev[0].tag == 77 && ev[0].frame >= 44098 && ev[0].frame <= 44100);
    m.set_bus_volume(na::Bus::Music, 0.5f);
    m.play(tone(1000, 10000), {na::Bus::Music, 1.0f, -1.0f});  // so a esquerda, a meio volume
    std::vector<int16_t> o2(200);
    m.render(o2);
    BS_CHECK(o2[0] == 5000 && o2[1] == 0);
}

// laco por pontos, pausa (o tempo da voz para), pausa geral, parada com evento
BS_TEST(audio_mixer_loops_pause_stop) {
    na::AudioMixer m(44100, 8);
    auto           s = tone(1000, 1000);
    auto           l = std::make_shared<na::SoundData>(*s);
    l->loopStart = 200, l->loopEnd = 600;
    const auto v = m.play(l, {na::Bus::Music, 1.0f, 0.0f, 0, true, 5});
    std::vector<int16_t> out(2 * 2000);
    m.render(out);  // 2000 quadros: passa do fim do laco varias vezes
    auto ev = m.take_events();
    BS_CHECK(m.playing(v) && ev.size() >= 4 && ev[0].kind == na::EventKind::Looped && ev[0].frame == 600);
    m.pause(v, true);
    std::vector<int16_t> o2(2 * 500);
    m.render(o2);
    BS_CHECK(o2[0] == 0 && m.take_events().empty());  // pausada: silencio e nenhum laco
    m.pause(v, false);
    m.pause_all(true);
    m.render(o2);
    BS_CHECK(o2[0] == 0 && m.playing(v));
    m.pause_all(false);
    m.stop(v);
    ev = m.take_events();
    BS_CHECK(!m.playing(v) && ev.size() == 1 && ev[0].kind == na::EventKind::Stopped && ev[0].tag == 5);
}

// limite de vozes: rouba a de menor prioridade (a mais antiga), recusa a de prioridade menor
BS_TEST(audio_mixer_priority_and_stealing) {
    na::AudioMixer m(44100, 2);
    const auto     a = m.play(tone(44100, 100), {na::Bus::Effects, 1, 0, 1});
    const auto     b = m.play(tone(44100, 100), {na::Bus::Effects, 1, 0, 3});
    BS_CHECK(a && b && m.active() == 2);
    BS_CHECK(m.play(tone(100, 1), {na::Bus::Effects, 1, 0, 0}) == 0);  // prioridade menor que todas
    const auto c = m.play(tone(100, 1), {na::Bus::Effects, 1, 0, 2});
    auto       ev = m.take_events();
    BS_CHECK(c && !m.playing(a) && m.playing(b) && ev.size() == 1 && ev[0].kind == na::EventKind::Stolen && ev[0].voice == a);
}

// catalogo: original do Builder, mods por idioma (exato, base, neutro), ausente, caminho fora recusado,
// sincronia da dublagem
BS_TEST(audio_catalog_mods_and_languages) {
    Dir d;
    const fs::path data = d.p / "data" / "audio", mods = d.p / "mods" / "audio";
    fs::create_directories(data / "voz"), fs::create_directories(mods / "pt-br" / "voz"), fs::create_directories(mods / "pt" / "voz");
    fs::create_directories(mods / "musica");
    std::ofstream(data / "voz" / "E001A.wav") << "x";
    std::ofstream(data / "manifest.toml") << "[[som]]\nid = \"voz/E001A\"\narquivo = \"voz/E001A.wav\"\nduracao_s = 7.86\n"
                                              "[[som]]\nid = \"voz/E001B\"\narquivo = \"../../fora.wav\"\n";
    std::ofstream(mods / "pt-br" / "voz" / "E001A.wav") << "x";
    std::ofstream(mods / "pt" / "voz" / "E001A.ogg") << "x";
    std::ofstream(mods / "musica" / "M2A.flac") << "x";
    std::ofstream(mods / "manifest.toml") << "[[som]]\nid = \"musica/M2A\"\nsincronia = \"arquivo\"\n";
    const auto c = na::SoundCatalog::load({data, mods});
    BS_CHECK(c.problems().size() == 1);  // E001B fora da pasta
    auto e = c.resolve("voz/E001A", "PT-BR");
    BS_CHECK(e && e->mod && e->lang == "pt-br" && e->file.extension() == ".wav" && e->originalSeconds > 7.8);
    e = c.resolve("voz/E001A", "pt-PT");  // sem pt-pt: cai no pt (base)
    BS_CHECK(e && e->mod && e->lang == "pt" && e->file.extension() == ".ogg");
    e = c.resolve("voz/E001A", "en");  // sem ingles: o original do Builder
    BS_CHECK(e && !e->mod && e->file.filename() == "E001A.wav");
    e = c.resolve("musica/M2A", "en");  // mod neutro (remaster), sincronia pelo arquivo
    BS_CHECK(e && e->mod && e->lang.empty() && e->sync == na::SyncPolicy::File);
    BS_CHECK(!c.resolve("voz/E999", "pt") && !c.resolve("../x", "pt") && !c.resolve("voz/../../x", ""));
    BS_CHECK(!na::SoundCatalog::valid_id("sem_tipo") && na::SoundCatalog::valid_id("efeito/BS_SND:SOSB0:12"));
}

// dublagem com outra duracao: o jogo ve o fim original (fiel) ou o do arquivo (modificado), sem mexer no tempo
BS_TEST(audio_dub_duration_policy) {
    na::SoundEntry e;
    e.originalSeconds = 2.0;
    e.sync            = na::SyncPolicy::Original;
    BS_CHECK(na::logical_end_frames(e, 3 * 22050, 22050, 44100) == 88200);  // mod de 3 s: o jogo ve 2 s
    e.sync = na::SyncPolicy::File;
    BS_CHECK(na::logical_end_frames(e, 3 * 22050, 22050, 44100) == 132300);
    e.sync            = na::SyncPolicy::Original;
    e.originalSeconds = 0;  // sem duracao original conhecida: a do arquivo
    BS_CHECK(na::logical_end_frames(e, 22050, 22050, 44100) == 44100);
}

// revisao Codex 2026-10-20: laco com passo maior que o laco, emenda interpolando para o inicio do laco
BS_TEST(audio_mixer_loop_overrun_and_seam) {
    na::AudioMixer m(8000, 4);
    auto           s = std::make_shared<na::SoundData>();
    s->rate = 96000, s->pcm = {1000, 2000, 3000}, s->loopStart = 0, s->loopEnd = 1;  // passo 12 > laco de 1
    m.play(s, {na::Bus::Music, 1, 0, 0, true});
    std::vector<int16_t> out(2 * 100);
    m.render(out);  // sem ler fora (o modulo mantem a posicao no laco)
    BS_CHECK(out[198] == 1000);
    na::AudioMixer m2(44100, 4);
    auto           e = std::make_shared<na::SoundData>();
    e->rate = 22050, e->pcm = {0, 100, 200, 300, 9999}, e->loopStart = 0, e->loopEnd = 4;  // 9999 fica fora do laco
    m2.play(e, {na::Bus::Music, 1, 0, 0, true});
    std::vector<int16_t> o2(2 * 8);
    m2.render(o2);
    BS_CHECK(o2[2 * 7] == 150);  // posicao 3,5: entre 300 e o inicio do laco (0), nunca 9999
}

// fim logico (dublagem mais longa): LogicalEnd na posicao do original, o som continua ate o Finished;
// eventos so ate o quadro ja tocado
BS_TEST(audio_mixer_logical_end_and_event_release) {
    na::AudioMixer m(1000, 4);
    const auto     v = m.play(tone(300, 100, 1000), {na::Bus::Voice, 1, 0, 0, false, 9, 1.0f, 200});
    std::vector<int16_t> out(2 * 250);
    m.render(out);
    BS_CHECK(m.take_events(150).empty());  // a saida so tocou ate 150: o fim logico (200) ainda nao vale
    auto ev = m.take_events(250);
    BS_CHECK(ev.size() == 1 && ev[0].kind == na::EventKind::LogicalEnd && ev[0].frame == 200 && m.playing(v));
    m.render(out);
    ev = m.take_events();
    BS_CHECK(ev.size() == 1 && ev[0].kind == na::EventKind::Finished && ev[0].frame == 300 && !m.playing(v));
    // som mais curto que o fim logico: os dois no fim fisico
    m.play(tone(50, 100, 1000), {na::Bus::Voice, 1, 0, 0, false, 9, 1.0f, 200});
    m.render(out);
    ev = m.take_events();
    BS_CHECK(ev.size() == 2 && ev[0].kind == na::EventKind::LogicalEnd && ev[1].kind == na::EventKind::Finished);
}

// pan e pitch mudam durante a reproducao; construtor com taxa e limite zero
BS_TEST(audio_mixer_mutable_params_and_ctor) {
    na::AudioMixer m(1000, 4);
    const auto     v = m.play(tone(1000, 1000, 1000), {na::Bus::Effects});
    m.set_pan(v, 1.0f);
    std::vector<int16_t> out(2 * 10);
    m.render(out);
    BS_CHECK(out[0] == 0 && out[1] == 1000);
    m.set_pitch(v, 2.0f);  // o dobro da velocidade: acaba na metade do tempo restante
    std::vector<int16_t> big(2 * 1000);
    m.render(big);
    auto ev = m.take_events();
    BS_CHECK(ev.size() == 1 && ev[0].kind == na::EventKind::Finished && ev[0].frame >= 500 && ev[0].frame <= 510);
    na::AudioMixer z(0, 0);  // normalizado: 44100 Hz e 1 voz, sem divisao por zero
    BS_CHECK(z.play(tone(100000, 1), {}) != 0);  // longa: ocupa a unica voz durante o teste
    std::vector<int16_t> o(2 * 64);
    z.render(o);
    BS_CHECK(z.play(tone(10, 1), {na::Bus::Effects, 1, 0, -1}) == 0);  // limite 1: prioridade menor recusada
}

// IDs com ':' viram '#' no nome do arquivo (valido no Windows); '#' recusado no ID; caminho canonico
BS_TEST(audio_catalog_escape_and_canonical) {
    Dir d;
    const fs::path mods = d.p / "mods" / "audio";
    fs::create_directories(mods / "efeito");
    std::ofstream(mods / "efeito" / "BS_SND#SOSB0#12.wav") << "x";
    const auto c = na::SoundCatalog::load({d.p / "data" / "audio", mods});
    const auto e = c.resolve("efeito/BS_SND:SOSB0:12");
    BS_CHECK(e && e->mod && e->file.filename() == "BS_SND#SOSB0#12.wav" && e->file.is_absolute());
    BS_CHECK(!na::SoundCatalog::valid_id("efeito/BS_SND#SOSB0#12"));
}

// fontes degeneradas: vazia, 1 quadro, laco de tamanho 0 e canais 0 nao travam nem leem fora do buffer
BS_TEST(audio_mixer_empty_and_tiny_sources) {
    na::AudioMixer       m(44100, 8);
    std::vector<int16_t> out(2 * 256);
    auto                 empty = std::make_shared<na::SoundData>();
    const auto           ve    = m.play(empty, {na::Bus::Effects, 1.0f, 0.0f, 0, false, 1});
    m.render(out);
    BS_CHECK(!m.playing(ve));
    BS_CHECK(out[0] == 0 && out[511] == 0);
    const auto v1 = m.play(tone(1, 9000), {na::Bus::Effects, 1.0f, 0.0f, 0, false, 2});
    m.render(out);
    BS_CHECK(!m.playing(v1) && out[0] == 9000 && out[4] == 0);
    auto loop0 = tone(100, 1000);
    loop0->loopStart = loop0->loopEnd = 40;  // laco vazio: tratado como sem pontos ou recusado, nunca laco infinito
    const auto vl = m.play(loop0, {na::Bus::Effects, 1.0f, 0.0f, 0, true, 3});
    m.render(out);
    m.stop(vl);
    auto noch      = tone(10, 1000);
    noch->channels = 0;
    m.play(noch, {});
    m.render(out);
    m.play(nullptr, {});
    m.render(out);
    BS_CHECK(m.active() == 0);
    for (const auto& e : m.take_events()) BS_CHECK(e.tag == 0 || e.tag <= 3);
}

// reamostragem nos extremos (8 kHz -> 96 kHz e 96 kHz -> 8 kHz) e pitch variavel: o fim cai no quadro esperado
BS_TEST(audio_mixer_extreme_rates_and_pitch) {
    struct Case { uint32_t src, out; float pitch; };
    for (const Case c : {Case{8000, 96000, 1.0f}, Case{96000, 8000, 1.0f}, Case{22050, 44100, 0.01f}, Case{22050, 44100, 16.0f}, Case{22050, 44100, 64.0f}}) {
        na::AudioMixer m(c.out, 4);
        const uint32_t frames = 4000;
        const auto     v      = m.play(tone(frames, 5000, c.src), {na::Bus::Effects, 1.0f, 0.0f, 0, false, 9, c.pitch});
        const double   expect = double(frames) * c.out / (double(c.src) * std::min(c.pitch, 16.0f));  // limite do mixer: 16
        std::vector<int16_t> out(2 * 4096);
        uint64_t             end = 0;
        for (int i = 0; i < 2000 && !end; ++i) {
            m.render(out);
            for (const auto& e : m.take_events())
                if (e.voice == v && e.kind == na::EventKind::Finished) end = e.frame;
        }
        if (!(end > 0 && std::abs(double(end) - expect) <= 2.0 + expect * 1e-6))
            std::printf("  caso %u->%u pitch %g: fim %llu, esperado %.1f\n", c.src, c.out, c.pitch, (unsigned long long)end, expect);
        BS_CHECK(end > 0 && std::abs(double(end) - expect) <= 2.0 + expect * 1e-6);
    }
    // pitch mudando no meio: 1 s a 1x, depois 2x; a posicao continua
    na::AudioMixer m(44100, 4);
    const auto     v = m.play(tone(44100 * 2, 3000), {na::Bus::Effects, 1.0f, 0.0f, 0, false, 5});
    std::vector<int16_t> half(2 * 44100);
    m.render(half);
    m.set_pitch(v, 2.0f);
    std::vector<int16_t> rest(2 * 22100);
    m.render(rest);
    auto ev = m.take_events();
    BS_CHECK(ev.size() == 1 && ev[0].kind == na::EventKind::Finished && ev[0].frame >= 44100 + 22048 && ev[0].frame <= 44100 + 22052);
    // pitch invalido (0, negativo, NaN) nao trava o tempo nem gera NaN na saida
    for (float bad : {0.0f, -1.0f, std::nanf("")}) {
        na::AudioMixer mb(44100, 2);
        mb.play(tone(100, 4000), {na::Bus::Effects, 1.0f, 0.0f, 0, false, 6, bad});
        std::vector<int16_t> o(2 * 512);
        mb.render(o);
        BS_CHECK(mb.now() == 512);
    }
}

// WAV invalidos: erro claro, sem leitura fora do buffer (cabecalhos truncados, tamanhos mentirosos, formatos impossiveis)
BS_TEST(audio_wav_invalid_inputs) {
    std::string err;
    BS_CHECK(!na::parse_wav(std::vector<uint8_t>{}, &err) && !err.empty());
    std::vector<uint8_t> pcm16(64, 1);
    auto good = wavBytes(1, 1, 22050, 16, pcm16);
    for (size_t cut : {8u, 12u, 20u, 36u, 40u}) BS_CHECK(!na::parse_wav(std::span(good.data(), cut), &err));
    auto lie = good;  // data diz 1 MB, o arquivo tem 64 bytes: le so o que existe ou recusa
    lie[lie.size() - 64 - 4] = 0x00, lie[lie.size() - 64 - 3] = 0x00, lie[lie.size() - 64 - 2] = 0x10;
    auto l = na::parse_wav(lie, &err);
    BS_CHECK(!l || l->frames() <= 32);
    BS_CHECK(!na::parse_wav(wavBytes(1, 0, 22050, 16, pcm16), &err));   // 0 canais
    BS_CHECK(!na::parse_wav(wavBytes(1, 1, 0, 16, pcm16), &err));       // taxa 0
    BS_CHECK(!na::parse_wav(wavBytes(1, 1, 22050, 24, pcm16), &err));   // 24 bits: sem suporte
    auto odd = na::parse_wav(wavBytes(1, 2, 22050, 16, std::vector<uint8_t>(6, 1)), &err);  // 1,5 quadro estereo
    BS_CHECK(!odd || odd->pcm.size() % 2 == 0);
}
