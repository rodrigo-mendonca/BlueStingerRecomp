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

// A soltura provisoria do envelope compartilhado (audio/envelope.h): exatamente 43 ticks de 4 ms ate -60 dB (172 ms)
BS_TEST(audio_envelope_provisional_release) {
    na::Envelope e;
    e.key_on();
    BS_CHECK(!e.tick() && e.gain() == 1.0f && !e.done());
    e.key_off();
    int ticks = 0;
    while (!e.done() && ticks < 1000) e.tick(), ++ticks;
    BS_CHECK(ticks == 43);
}
