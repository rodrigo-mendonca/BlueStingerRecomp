// N6 fase F: o sintetizador do Manatee (native/audio/manatee_synth). O tom contra as tabelas do driver (0x3504 oitava e
// semitom, 0x35E8 FNS = floor(1024 * (2^(j/1536) - 1)), conferida nas 1536 entradas do MANATEE.DRV do disco); a escolha
// das camadas (uma por grupo, grupos juntos) e o pedal 64.
#include "test.h"

#include "audio/manatee_seq.h"
#include "audio/manatee_synth.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace nm = bs::native::audio::manatee;
namespace na = bs::native::audio;

BS_TEST(audio_manatee_synth_pitch) {
    auto p = nm::pitch_for(60, 60, 0);
    BS_CHECK(p.oct == 0 && p.fns == 0 && !p.saturated);
    p = nm::pitch_for(72, 60, 0);
    BS_CHECK(p.oct == 1 && p.fns == 0);
    p = nm::pitch_for(47, 60, 0);  // -13 semitons: oitava -2, semitom 11
    BS_CHECK(p.oct == -2 && p.fns == uint32_t(std::floor(1024 * (std::exp2(11 / 12.0) - 1))));
    p = nm::pitch_for(61, 60, 0);  // 1 semitom: FNS 60 (1024 * 0,0595 = 60,9 -> piso)
    BS_CHECK(p.oct == 0 && p.fns == 60);
    p = nm::pitch_for(60, 60, 127);  // fino positivo: j = 63
    BS_CHECK(p.oct == 0 && p.fns == uint32_t(std::floor(1024 * (std::exp2(63 / 1536.0) - 1))));
    p = nm::pitch_for(60, 60, -1);  // fino -1/256: o semitom de baixo com j = 11 * 128 + 127 (como o driver soma)
    BS_CHECK(p.oct == -1 && p.fns == uint32_t(std::floor(1024 * (std::exp2((11 * 128 + 127) / 1536.0) - 1))));
    BS_CHECK(nm::pitch_for(127, 0, 0).saturated);  // +127 semitons: alem da tabela do driver
    BS_CHECK(std::abs(nm::pitch_for(72, 60, 0).ratio() - 2.0) < 1e-9);
}

namespace {
// banco sintetico: programa 0 com dois grupos (o 1o com duas faixas de teclas), amostras ADPCM de silencio com laco
struct Banco {
    std::vector<uint8_t> dados = std::vector<uint8_t>(4096, 0x11);
    nm::Bank             b;
    nm::DriverTables     drv;  // curva de volume sintetica (a do disco no teste com BS_GAME_DATA)
    Banco() {
        for (int i = 0; i < 128; ++i) drv.volume[i] = uint8_t(i);
        b.data = dados;
        b.velocityCurves.push_back({});
        for (int i = 0; i < 128; ++i) b.velocityCurves[0][i] = uint8_t(i * 2);
        auto camada = [](uint32_t sa, uint8_t k0, uint8_t k1) {
            nm::Layer l;
            l.sa = sa, l.flags = 3, l.lsa = 0, l.lea = 1000, l.keyMin = k0, l.keyMax = k1, l.keyBase = 60;
            l.eg10 = 31, l.eg14 = 31;  // envelope dos efeitos (AR 31, RR 31): ataque imediato, soltura em ~3,6 ms
            return l;
        };
        nm::Program p;
        p.groups.resize(2);
        p.groups[0].layers = {camada(0, 0, 59), camada(1024, 60, 127)};
        p.groups[1].layers = {camada(2048, 40, 80)};
        b.programs.push_back(p);
    }
};
nm::MusicEvent ev(nm::MusicEvent::Kind k, uint8_t a, uint8_t b, uint32_t id = 0) {
    nm::MusicEvent e{};
    e.kind = k, e.canal = 0, e.a = a, e.b = b, e.noteId = id;
    return e;
}
} // namespace

BS_TEST(audio_manatee_synth_layers) {
    Banco         bc;
    na::AudioMixer mx(44100, 32);
    nm::Synth      s(mx, bc.b, bc.drv);
    using K = nm::MusicEvent::Kind;
    s.on_event(ev(K::NoteOn, 64, 100, 1));  // grupo 0 (faixa alta) + grupo 1
    BS_CHECK(s.sounding() == 2);
    s.on_event(ev(K::NoteOn, 30, 100, 2));  // so o grupo 0 (faixa baixa)
    BS_CHECK(s.sounding() == 3 && s.stats().voices == 3 && s.stats().noLayer == 0);
    s.on_event(ev(K::Program, 5, 0));
    s.on_event(ev(K::NoteOn, 64, 100, 3));  // programa inexistente
    BS_CHECK(s.stats().noLayer == 1 && s.sounding() == 3);
}

BS_TEST(audio_manatee_synth_sustain) {
    Banco          bc;
    na::AudioMixer mx(44100, 32);
    nm::Synth      s(mx, bc.b, bc.drv);
    using K = nm::MusicEvent::Kind;
    s.on_event(ev(K::Control, 64, 127));
    s.on_event(ev(K::NoteOn, 30, 100, 1));
    s.on_event(ev(K::NoteOff, 30, 0, 1));
    for (int i = 0; i < 100; ++i) s.control_tick();
    BS_CHECK(s.sounding() == 1);  // o pedal segura
    s.on_event(ev(K::Control, 64, 0));
    for (int i = 0; i < 100; ++i) s.control_tick();
    BS_CHECK(s.sounding() == 0);  // solto: soltura
}

namespace {
std::vector<uint8_t> ler_arquivo(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}
} // namespace

// Com BS_GAME_DATA: as tabelas do MANATEE.DRV do disco (versao conferida pela tabela de FNS) e a lei de nivel contra o
// TL dos key-on da AICA medidos no Flycast (oraculo M2A, 2026-10-20; curva da camada = a velocidade, porta em -16).
BS_TEST(audio_manatee_synth_level_vs_oracle) {
    const char* gd = std::getenv("BS_GAME_DATA");
    if (!gd) BS_SKIP("BS_GAME_DATA ausente");
    std::string err;
    const auto  drvBytes = ler_arquivo(std::filesystem::path(gd) / "BSDATA" / "MANATEE.DRV");
    auto        drv      = nm::parse_driver_tables(drvBytes, &err);
    BS_CHECK(drv.has_value());
    if (!drv) return;
    auto torto = drvBytes;
    torto[0x20 + 0x35E8 + 700] ^= 1;
    BS_CHECK(!nm::parse_driver_tables(torto));  // outra versao do driver e recusada
    struct Caso {
        uint8_t cc7, curva, tlCamada, tlOraculo;
    };
    const Caso casos[] = {{84, 127, 0, 44}, {80, 127, 0, 48}, {67, 127, 0, 60}, {60, 127, 0, 66}, {60, 127, 8, 72},
                          {84, 127, 22, 63}, {84, 112, 0, 70}, {84, 112, 26, 91}, {90, 123, 0, 47}, {80, 112, 0, 74}};
    for (const Caso& k : casos) {
        Banco bc;
        for (int i = 0; i < 128; ++i) bc.b.velocityCurves[0][i] = uint8_t(i);
        for (auto& g : bc.b.programs[0].groups)
            for (auto& l : g.layers) l.tl = k.tlCamada;
        na::AudioMixer mx(44100, 32);
        nm::Synth      s(mx, bc.b, *drv);
        s.set_port_volume(-16);
        int tl = -1;
        s.onVoice = [&](const nm::VoiceLog& v) { tl = v.tl; };
        s.on_event(ev(nm::MusicEvent::Kind::Control, 7, k.cc7));
        s.on_event(ev(nm::MusicEvent::Kind::NoteOn, 30, k.curva, 1));
        BS_CHECK(tl == k.tlOraculo);
        if (tl != k.tlOraculo) std::printf("  cc7 %u curva %u tl %u: %d (oraculo %u)\n", k.cc7, k.curva, k.tlCamada, tl, k.tlOraculo);
    }
}

// Com BS_GAME_DATA: as 11 musicas do disco por 40 s cada no sequenciador, no sintetizador e no mixer (o caminho
// inteiro, tambem com o ASan): nenhuma voz e recusada, nenhum tom sai da tabela do driver e as notas sem camada sao so as
// que o driver tambem nao toca (0x0B78): na M2B, 6 notas (teclas 98 e 100) no programa 3, que so cobre 0-34 e 59-82; na
// M3, 1 nota (tecla 96) no programa 2, que so cobre 0-82 (nos primeiros 40 s).
BS_TEST(audio_manatee_synth_real_songs) {
    const char* gd = std::getenv("BS_GAME_DATA");
    if (!gd) BS_SKIP("BS_GAME_DATA ausente");
    const std::filesystem::path dir = std::filesystem::path(gd) / "BSDATA";
    const auto                  drvBytes = ler_arquivo(dir / "MANATEE.DRV");
    auto                        drv      = nm::parse_driver_tables(drvBytes);
    BS_CHECK(drv.has_value());
    if (!drv) return;
    for (const char* nome : {"M2A", "M2B", "M2C", "M3", "M4", "M5", "M6", "M7", "M9", "M10", "M99"}) {
        const auto msb = ler_arquivo(dir / (std::string(nome) + ".MSB")), mpb = ler_arquivo(dir / (std::string(nome) + ".MPB"));
        auto       seqs = nm::parse_smsb(msb);
        auto       bank = nm::parse_smpb(mpb);
        BS_CHECK(seqs && bank);
        if (!seqs || !bank) continue;
        na::AudioMixer              mx(44100, 64);
        nm::Sequencer               seq((*seqs)[0]);
        nm::Synth                   s(mx, *bank, *drv);
        std::vector<nm::MusicEvent> ev;
        std::vector<int16_t>        buf;
        for (uint64_t c = 1; c <= 40 * 250; ++c) {
            ev.clear();
            seq.call(ev);
            for (const auto& e : ev) s.on_event(e);
            s.control_tick();
            buf.assign(size_t(c * 44100 * 2 / 500 - (c - 1) * 44100 * 2 / 500) * 2, 0);  // 176,4 amostras por chamada
            mx.render(buf);
        }
        const auto&    st        = s.stats();
        const uint64_t semCamada = std::string(nome) == "M2B" ? 6 : std::string(nome) == "M3" ? 1 : 0;
        BS_CHECK(seq.stolen() == 0);  // a lista de 48 notas do driver nunca enche nas musicas do disco
        BS_CHECK(st.notes > 0 && st.noLayer == semCamada && st.refused == 0 && st.saturatedPitch == 0 && st.decodeFailures == 0);
        std::printf("  %s: %llu notas, %llu vozes, sem camada %llu, recusadas %llu, tom fora %llu, amostra %llu\n", nome,
                    static_cast<unsigned long long>(st.notes), static_cast<unsigned long long>(st.voices),
                    static_cast<unsigned long long>(st.noLayer), static_cast<unsigned long long>(st.refused),
                    static_cast<unsigned long long>(st.saturatedPitch), static_cast<unsigned long long>(st.decodeFailures));
    }
}

// Revisao Codex 2026-10-20 (MEDIA): duas camadas com o mesmo SA e outro laco nao dividem a amostra decodificada
BS_TEST(audio_manatee_synth_cache_key) {
    Banco bc;
    auto& g = bc.b.programs[0].groups;
    g[1].layers[0].sa = g[0].layers[1].sa;  // mesmo SA da camada alta do grupo 0 ...
    g[1].layers[0].lsa = 100;               // ... com outro inicio de laco
    na::AudioMixer mx(44100, 32);
    nm::Synth      s(mx, bc.b, bc.drv);
    std::vector<int> lacos;
    s.onVoice = [&](const nm::VoiceLog& v) { lacos.push_back(v.loop); };
    s.on_event(ev(nm::MusicEvent::Kind::NoteOn, 64, 100, 1));
    BS_CHECK(s.sounding() == 2 && s.stats().voices == 2);
    BS_CHECK(s.cached() == 2);  // duas decodificacoes, uma por descritor
}

// Envelope da AICA (audio/envelope.h) contra as inclinacoes MEDIDAS no oraculo (Flycast, MSLC certo; EG por passo das
// vozes da M2A, docs/N6-PLANO.md secao 19): soltura de 0 a 0x3BF, unidades por segundo, por (RR, OCT, FNS[9]).
BS_TEST(audio_envelope_release_rates_vs_oracle) {
    struct Medida {
        uint8_t rr;
        int     oct;
        uint32_t fns;
        double  porSegundo;  // mediana medida no oraculo
    };
    const Medida medidas[] = {{16, -1, 0, 1479.2},  {16, -2, 512, 1859.0}, {16, -1, 512, 1859.1}, {15, -1, 0, 1111.1},
                              {15, -2, 512, 1298.6}, {22, -1, 0, 12048.2}, {22, -2, 512, 15064.5}, {17, -1, 0, 2221.6},
                              {18, -1, 0, 3014.5},  {19, -2, 0, 4453.0},  {19, -2, 512, 5120.1}, {20, -1, 0, 6025.6}};
    for (const auto& m : medidas) {
        na::EnvelopeParams p;
        p.ar = 31, p.rr = m.rr, p.oct = m.oct, p.fns = m.fns;
        na::Envelope e;
        e.key_on(p);
        BS_CHECK(e.level() == 0 && e.gain() == 1.0f);
        e.key_off();
        const double dt = 100.0 / m.porSegundo;  // 100 unidades
        e.advance(dt);
        BS_CHECK(std::abs(e.level() / dt / m.porSegundo - 1.0) < 0.02);
        if (std::abs(e.level() / dt / m.porSegundo - 1.0) >= 0.02)
            std::printf("  RR %u oct %d fns %u: %.1f un/s (oraculo %.1f)\n", m.rr, m.oct, m.fns, e.level() / dt, m.porSegundo);
    }
}

// Decaimento 1 ate DL << 5 e depois o decaimento 2 (D2R 0 segura); a soltura acaba a voz depois de 0x3BF
BS_TEST(audio_envelope_states) {
    na::EnvelopeParams p;
    p.ar = 31, p.d1r = 18, p.d2r = 0, p.dl = 3, p.rr = 16, p.oct = -1;
    na::Envelope e;
    e.key_on(p);
    e.advance(1.0);  // D1R 18 -> taxa 36: 340 ms por 1 024 unidades; 96 unidades bem antes de 1 s
    BS_CHECK(e.state() == na::Envelope::State::Decay2 && e.level() == 96.0);
    e.advance(10.0);
    BS_CHECK(e.level() == 96.0);  // D2R 0: segura
    e.key_off();
    e.advance(2.0);
    BS_CHECK(e.done() && e.gain() == 0.0f);
    // os efeitos (os 211 descritores do BS_SND.MLT: AR 31, D1R 0, D2R 0, RR 31, DL 0): cheio ate o key-off ...
    na::EnvelopeParams fx;
    fx.ar = 31, fx.rr = 31, fx.oct = -2, fx.fns = 461;
    na::Envelope f;
    f.key_on(fx);
    f.advance(5.0);
    BS_CHECK(f.gain() == 1.0f && !f.done());
    f.key_off();  // ... e a soltura mais rapida: taxa 62, 3,6 ms para a faixa
    f.advance(0.0036 * 0x3C0 / 1024.0 + 0.0001);
    BS_CHECK(f.done());
    // ataque 25 (taxa 50): 2 ms
    na::EnvelopeParams lento;
    lento.ar = 25;
    na::Envelope a;
    a.key_on(lento);
    BS_CHECK(a.state() == na::Envelope::State::Attack && a.gain() < 0.01f);
    a.advance(0.0021);
    BS_CHECK(a.level() == 0.0);
}

// Pitch bend pelo driver (0xE0 -> 0x13E4, key-on 0x113C): bend = valor * 128 - 8192, faixa dos bytes 0x0C/0x0D do grupo
// (0x202 nos 11 bancos: 2 semitons para cima e para baixo), acc = fino + ((bend * faixa) >> 5). Conferido no tom EFETIVO
// da voz no mixer (AudioMixer::pitch_of) contra 2^(semitons/12), calculado a parte (revisao Codex 2026-10-20: a callback
// de diagnostico e a mesma pitch_for nao provavam o audio). O reset 121 zera o bend do canal (0x27C8) sem mexer no tom das
// vozes que ja soam (0x27FC-0x2860 so solta o pedal); a nota seguinte nasce sem bend.
BS_TEST(audio_manatee_synth_pitch_bend) {
    Banco bc;
    for (auto& g : bc.b.programs[0].groups) g.flags = 0x202;
    na::AudioMixer mx(44100, 32);
    nm::Synth      s(mx, bc.b, bc.drv);
    std::vector<na::VoiceId> vozes;
    s.onVoice = [&](const nm::VoiceLog& v) { vozes.push_back(v.voice); };
    using K = nm::MusicEvent::Kind;
    s.on_event(ev(K::NoteOn, 30, 100, 1));  // so o grupo 0 (faixa baixa): uma voz
    BS_CHECK(vozes.size() == 1);
    if (vozes.size() != 1) return;
    const float p0 = mx.pitch_of(vozes[0]);
    BS_CHECK(p0 > 0.0f);
    // razao esperada (sem pitch_for): 2^(semitons/12), semitons = (valor*128 - 8192) * 2 / 32 / 256
    auto confere = [&](na::VoiceId v, uint8_t valor) {
        const double semitons = (double(valor) * 128 - 8192) * 2 / 32 / 256;
        const double razao    = mx.pitch_of(v) / p0, esperada = std::exp2(semitons / 12.0);
        return std::abs(razao / esperada - 1.0) < 0.002;  // FNS em 1/1024 de oitava: < 0,1 % de quantizacao
    };
    for (uint8_t valor : {0, 32, 48, 64, 96, 127}) {
        s.on_event(ev(K::Pitch, valor, 0));
        BS_CHECK(confere(vozes[0], valor));
    }
    s.on_event(ev(K::Pitch, 0, 0));  // -2 semitons
    BS_CHECK(confere(vozes[0], 0));
    s.on_event(ev(K::NoteOn, 30, 100, 2));  // nota nova ja nasce com o bend (0x114C)
    BS_CHECK(vozes.size() == 2 && confere(vozes[1], 0));
    s.on_event(ev(K::Control, 121, 0));  // reset: as vozes que soam continuam no tom delas ...
    BS_CHECK(confere(vozes[0], 0) && confere(vozes[1], 0));
    s.on_event(ev(K::NoteOn, 30, 100, 3));  // ... e a nota seguinte nasce sem bend, sem nenhum evento de pitch antes
    BS_CHECK(vozes.size() == 3 && mx.pitch_of(vozes[2]) == p0);
}

// os 48 canais sao um conjunto so para todas as portas (0xE800 do driver; revisao Codex 2026-10-21): duas portas com 30
// notas ligadas cada ficam em 48 vozes no total, e a segunda reaproveita as mais antigas da primeira
BS_TEST(audio_manatee_synth_shared_pool) {
    Banco           bc;
    na::AudioMixer  mx(44100, 80);
    nm::ChannelPool pool;
    nm::Synth       a(mx, bc.b, bc.drv), b(mx, bc.b, bc.drv);
    a.set_pool(&pool), b.set_pool(&pool);
    pool.members = {&a, &b};
    using K = nm::MusicEvent::Kind;
    for (uint32_t i = 0; i < 30; ++i) a.on_event(ev(K::NoteOn, 30, 100, i + 1));
    for (uint32_t i = 0; i < 30; ++i) b.on_event(ev(K::NoteOn, 30, 100, 100 + i));
    BS_CHECK(a.sounding() + b.sounding() == 48);
    BS_CHECK(a.sounding() == 18 && b.sounding() == 30);
    BS_CHECK(b.stats().stolen == 12 && b.stats().stolenKeyed == 12 && a.stats().stolen == 0);
}
