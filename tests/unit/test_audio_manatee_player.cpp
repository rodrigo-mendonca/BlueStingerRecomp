// N6 fase F: o lado do driver Manatee que o SDK comanda (native/audio/manatee_player): faders de volume/pan da porta
// pelo algoritmo de 0x4D90/0x6640, slots de download, tocar/parar e os erros do tocar. Os casos do fade sao os que o
// gerenciador de musica do jogo manda (8C02AD80: -113 com t = 0, -16 com t = 0, tocar, -16 com t = 2000; abaixar nas
// falas: -48 com t = 800).
#include "test.h"

#include "audio/manatee_player.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace nm = bs::native::audio::manatee;
namespace na = bs::native::audio;

namespace {
int ticks_ate_alvo(nm::PortFader& f, int limite = 100000) {
    for (int k = 1; k <= limite; ++k) {
        f.tick();
        if (f.current == f.target) return k;
    }
    return -1;
}
void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) { std::memcpy(b.data() + o, &v, 4); }
// SMSB com uma sequencia de cabecalho (136, tempo) e os eventos dados (como em test_audio_manatee_seq)
std::vector<uint8_t> smsb(uint32_t tempo, const std::vector<uint8_t>& ev) {
    std::vector<uint8_t> b(32 + ev.size(), 0);
    std::memcpy(b.data(), "SMSB", 4), put32(b, 4, 1), put32(b, 8, uint32_t(b.size())), put32(b, 12, 1), put32(b, 16, 20);
    std::memcpy(b.data() + 20, "SMSD", 4), put32(b, 24, 136), put32(b, 28, tempo);
    std::memcpy(b.data() + 32, ev.data(), ev.size());
    return b;
}
nm::DriverTables tabelas() {
    nm::DriverTables t;
    for (int i = 0; i < 128; ++i) t.volume[i] = uint8_t(i);
    return t;
}
} // namespace

BS_TEST(audio_manatee_fader_driver_algorithm) {
    nm::PortFader f;  // porta no inicio: 0x80 (0x52C)
    BS_CHECK(f.current == 0x80 && f.target == 0x80 && !f.tick());
    f.command(uint8_t(-113 + 0x80), 0);  // t = 0: no tick seguinte
    BS_CHECK(f.current == 0x80 && f.tick() && f.current == 15);
    // -113 -> -16 em t = 2000: d = 97, p = 250 -> metades ate d <= 1: periodo 3, passo 1 (0x4DD8)
    f.command(uint8_t(-16 + 0x80), 2000);
    BS_CHECK(f.period == 3 && f.step == 1 && f.countdown == 3);
    BS_CHECK(ticks_ate_alvo(f) == 97 * 4);  // 1 a cada 4 ticks de 4 ms: 1,552 s
    // abaixar nas falas: -16 -> -48 em t = 800: d = 32, p = 100 -> (16,50) (8,25) (4,12) (2,6) (1,3): periodo 3, passo 1
    f.command(uint8_t(-48 + 0x80), 800);
    BS_CHECK(f.period == 3 && f.step == 1);
    BS_CHECK(ticks_ate_alvo(f) == 32 * 4);
    // passo grande: d = 200, t = 16 -> p = 2: (100, 1): periodo 1, passo 100 (sem passar do alvo)
    nm::PortFader g;
    g.current = 0;
    g.command(200, 16);
    BS_CHECK(g.period == 1 && g.step == 100);
    g.tick(), g.tick();
    BS_CHECK(g.current == 100);
    g.tick(), g.tick();
    BS_CHECK(g.current == 200 && !g.tick());
    // fade substituido no meio: recomeca do valor atual (o comando novo recalcula passo e periodo)
    nm::PortFader h;
    h.command(0, 2000);  // 0x80 -> 0: d = 128, p = 250 -> (64,125) ... (1,3)? d: 128,64,32,16,8,4,2,1 / p: 250..1
    for (int k = 0; k < 40; ++k) h.tick();
    const uint8_t meio = h.current;
    BS_CHECK(meio < 0x80 && meio > 0);
    h.command(0x80, 0);
    BS_CHECK(h.tick() && h.current == 0x80);
    // periodo guardado em 8 bits (strb): d = 1 nao divide, t = 4000 -> p = 500 -> 244
    nm::PortFader q;
    q.current = 0x80;
    q.command(0x81, 4000);
    BS_CHECK(q.period == uint8_t(500) && q.step == 1);
}

BS_TEST(audio_manatee_player_play_stop) {
    na::AudioMixer    mx(44100, 64);
    nm::MusicPlayer   pl(mx, tabelas());
    std::string       err;
    // tipo que nao e da musica: fica com o original (o adaptador repassa)
    BS_CHECK(!pl.download(0x42534F53 /*'SOSB'*/, 0, {1, 2, 3}));
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::NotSmsb);  // slot vazio
    // notas 60 (gate 2) e 62, sem banco: os eventos saem do sequenciador; sem SMPB no slot nada soa (0x3DE8)
    BS_CHECK(pl.download(nm::kMagicSmsb, 0, smsb(480, {0x00, 60, 100, 2, 1, 0x00, 62, 100, 2, 1, 0x83}), &err));
    BS_CHECK(pl.play(0, 0, 1, 0) == nm::PlayError::NoSequence);
    std::vector<int> notas;
    pl.onEvent = [&](uint8_t porta, const nm::MusicEvent& e) {
        if (porta == 0 && e.kind == nm::MusicEvent::Kind::NoteOn) notas.push_back(e.a);
    };
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::None);
    BS_CHECK(pl.view(0).playing);
    for (int k = 0; k < 200; ++k) pl.tick();
    BS_CHECK((notas == std::vector<int>{60, 62}));
    BS_CHECK(pl.synth_stats().noLayer == 2);
    BS_CHECK(!pl.view(0).playing);  // 0x83
    // volume da porta persiste entre tocar (o tocar nao mexe nos faders)
    pl.volume(0, -16, 0);
    pl.tick();
    BS_CHECK(pl.view(0).volume == 0x70);
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::None && pl.view(0).volume == 0x70);
    pl.stop(0);
    BS_CHECK(!pl.view(0).playing && pl.stats().stops == 1);
    // SMSB ruim: o slot fica vazio
    BS_CHECK(pl.download(nm::kMagicSmsb, 0, {'S', 'M', 'S', 'B'}, &err));
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::NotSmsb && pl.stats().refusedDownloads == 1);
}

// Com BS_GAME_DATA: a M2A pelo caminho do jogo (download do MPB e do MSB no slot 0, volume -113, -16, tocar, fade para
// -16 em t = 2000): toca, as vozes saem com o banco do slot e o parar desliga tudo.
BS_TEST(audio_manatee_player_m2a) {
    const char* gd = std::getenv("BS_GAME_DATA");
    if (!gd) BS_SKIP("BS_GAME_DATA ausente");
    auto ler = [&](const char* n) {
        std::ifstream f(std::filesystem::path(gd) / "BSDATA" / n, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
    };
    auto drv = nm::parse_driver_tables(ler("MANATEE.DRV"));
    BS_CHECK(drv.has_value());
    if (!drv) return;
    na::AudioMixer  mx(44100, 64);
    nm::MusicPlayer pl(mx, *drv);
    BS_CHECK(pl.download(nm::kMagicSmpb, 0, ler("M2A.MPB")) && pl.download(nm::kMagicSmsb, 0, ler("M2A.MSB")));
    pl.volume(0, -113, 0), pl.pan(0, 0, 0), pl.stop(0), pl.volume(0, -16, 0);
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::None);
    pl.volume(0, -16, 2000);
    std::vector<int16_t> buf;
    for (uint64_t c = 1; c <= 250 * 10; ++c) {
        pl.tick();
        buf.assign(size_t(c * 882 / 5 - (c - 1) * 882 / 5) * 2, 0);
        mx.render(buf);
    }
    const auto s = pl.synth_stats();
    BS_CHECK(s.notes > 100 && s.noLayer == 0 && s.refused == 0 && pl.view(0).sounding > 0 && pl.view(0).volume == 0x70);
    pl.stop(0);
    for (int k = 0; k < 250; ++k) pl.tick(), buf.assign(352, 0), mx.render(buf);
    BS_CHECK(pl.view(0).sounding == 0);
    std::printf("  M2A pelo player: %llu notas, %llu vozes em 10 s\n", static_cast<unsigned long long>(s.notes),
                static_cast<unsigned long long>(s.voices));
}

// Com BS_GAME_DATA: trocar os slots com a porta tocando (o jogo sempre para antes, mas um mod ou uma ordem diferente nao
// pode deixar ponteiro pendente): a sequencia antiga segue viva na porta, o banco novo vale para as notas seguintes e as
// vozes que ja soam guardam a propria amostra. Com o ASan, nenhum acesso a memoria liberada.
BS_TEST(audio_manatee_player_slot_swap_while_playing) {
    const char* gd = std::getenv("BS_GAME_DATA");
    if (!gd) BS_SKIP("BS_GAME_DATA ausente");
    auto ler = [&](const char* n) {
        std::ifstream f(std::filesystem::path(gd) / "BSDATA" / n, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
    };
    auto drv = nm::parse_driver_tables(ler("MANATEE.DRV"));
    BS_CHECK(drv.has_value());
    if (!drv) return;
    na::AudioMixer       mx(44100, 64);
    nm::MusicPlayer      pl(mx, *drv);
    std::vector<int16_t> buf;
    auto                 anda = [&](int ticks) {
        for (int k = 0; k < ticks; ++k) pl.tick(), buf.assign(352, 0), mx.render(buf);
    };
    BS_CHECK(pl.download(nm::kMagicSmpb, 0, ler("M2A.MPB")) && pl.download(nm::kMagicSmsb, 0, ler("M2A.MSB")));
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::None);
    anda(500);
    const uint64_t notasAntes = pl.synth_stats().notes;
    BS_CHECK(pl.download(nm::kMagicSmsb, 0, ler("M4.MSB")) && pl.download(nm::kMagicSmpb, 0, ler("M4.MPB")));
    anda(500);  // a M2A segue (dados antigos na porta) com o banco da M4
    BS_CHECK(pl.view(0).playing && pl.synth_stats().notes > notasAntes);
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::None);  // agora a M4
    anda(500);
    BS_CHECK(pl.synth_stats().refused == 0);
    pl.stop(0);
    anda(300);
    BS_CHECK(pl.view(0).sounding == 0);
}

// Revisao Codex 2026-10-20 (MEDIA): os ids das notas continuam entre um tocar e outro na mesma porta, entao o desliga de
// uma nota nova nunca alcanca uma voz que sobrou do tocar anterior (o tocar nao desliga as vozes, 0x4B3C).
BS_TEST(audio_manatee_player_note_ids_across_plays) {
    na::AudioMixer  mx(44100, 64);
    nm::MusicPlayer pl(mx, tabelas());
    BS_CHECK(pl.download(nm::kMagicSmsb, 0, smsb(480, {0x00, 60, 100, 200, 1, 0x00, 62, 100, 200, 1, 0x83})));
    std::vector<uint32_t> ids;
    pl.onEvent = [&](uint8_t, const nm::MusicEvent& e) {
        if (e.kind == nm::MusicEvent::Kind::NoteOn) ids.push_back(e.noteId);
    };
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::None);
    for (int k = 0; k < 100; ++k) pl.tick();
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::None);  // de novo, com as notas longas da 1a ainda sem desliga
    for (int k = 0; k < 100; ++k) pl.tick();
    pl.stop(0);
    BS_CHECK(pl.play(0, 0, 0, 0) == nm::PlayError::None);  // depois do parar tambem continua
    for (int k = 0; k < 100; ++k) pl.tick();
    BS_CHECK((ids == std::vector<uint32_t>{1, 2, 3, 4, 5, 6}));
}
