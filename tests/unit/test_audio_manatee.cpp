// N6 fase F: leitura das musicas do Manatee (native/audio/manatee). Sinteticos sempre; com BS_GAME_DATA, as 11 musicas e
// os 11 bancos do disco do usuario com as mesmas contagens que tools/dev/n6_smsd.py e n6_smpb.py acharam; mutacoes ao
// acaso nunca saem dos limites (com o ASan).
#include "test.h"

#include "audio/manatee.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace nm = bs::native::audio::manatee;

namespace {
void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) { std::memcpy(b.data() + o, &v, 4); }
// SMSB com uma sequencia: tempo, controle, programa, nota de gate 1 byte, nota de gate 2 bytes e delta 2 bytes,
// pitch, laco, referencia, um byte ignorado e o fim
std::vector<uint8_t> smsb_sintetico() {
    const std::vector<uint8_t> ev = {0x84, 0x01, 0xA6, 0x00,                    // tempo 422, delta 0
                                     0xB3, 0x07, 0x64, 0x00,                    // controle canal 3: 7 = 100
                                     0xC3, 0x05, 0x00,                          // programa 5
                                     0x03, 0x3C, 0x50, 0x30, 0x02,              // nota canal 3, tecla 60, vel 80, gate 0x30, delta 2
                                     0x33, 0x40, 0x60, 0x01, 0xE0, 0x01, 0x00,  // gate de 2 bytes (480), delta de 2 bytes (256)
                                     0xE3, 0x40, 0x00,                          // pitch
                                     0x82, 0x00, 0x00,                          // laco sem fim
                                     0x81, 0x00, 0x00, 0x02,                    // referencia: 2 eventos do inicio
                                     0x90,                                      // ignorado pelo driver
                                     0x83};
    std::vector<uint8_t> b(16 + 4 + 12 + ev.size(), 0);
    std::memcpy(b.data(), "SMSB", 4), put32(b, 4, 1), put32(b, 8, uint32_t(b.size())), put32(b, 12, 1), put32(b, 16, 20);
    std::memcpy(b.data() + 20, "SMSD", 4), put32(b, 24, 0x88), put32(b, 28, 0x1A6);
    std::memcpy(b.data() + 32, ev.data(), ev.size());
    return b;
}
std::vector<uint8_t> ler(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}
} // namespace

BS_TEST(audio_manatee_smsd_events) {
    std::string err;
    auto        s = nm::parse_smsb(smsb_sintetico(), &err);
    BS_CHECK(s && s->size() == 1);
    if (!s) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    const auto& e = (*s)[0].events;
    using K       = nm::SeqEvent::Kind;
    BS_CHECK(e.size() == 10 && (*s)[0].header2 == 0x1A6);  // tempo, controle, programa, 2 notas, pitch, laco, ref, ignorado, fim
    BS_CHECK(e[0].kind == K::Tempo && e[0].ref == 422);
    BS_CHECK(e[1].kind == K::Control && e[1].canal == 3 && e[1].a == 7 && e[1].b == 100);
    BS_CHECK(e[2].kind == K::Program && e[2].a == 5);
    BS_CHECK(e[3].kind == K::Note && e[3].canal == 3 && e[3].a == 60 && e[3].b == 80 && e[3].gate == 0x30 && e[3].delta == 2);
    BS_CHECK(e[4].kind == K::Note && e[4].gate == 480 && e[4].delta == 256);
    BS_CHECK(e[5].kind == K::Pitch && e[6].kind == K::Loop && e[6].count == 0 && e[7].kind == K::Reference && e[7].count == 2);
    BS_CHECK(e[8].kind == K::Ignored && e[9].kind == K::End);
}

BS_TEST(audio_manatee_rejects_bad_input) {
    std::string err;
    auto        b = smsb_sintetico();
    BS_CHECK(!nm::parse_smsb(std::vector<uint8_t>(b.begin(), b.end() - 1), &err));    // sem o 0x83
    BS_CHECK(!nm::parse_smsb(std::vector<uint8_t>(b.begin(), b.begin() + 40), &err));  // evento cortado
    auto x = b;
    put32(x, 16, 0x7FFFFFF0);
    BS_CHECK(!nm::parse_smsb(x, &err));
    BS_CHECK(!nm::parse_smpb(std::vector<uint8_t>(64, 0), &err));
    std::mt19937 rng(181);  // mutacoes: nunca saem dos limites (o ASan confere)
    for (int k = 0; k < 2000; ++k) {
        auto m = b;
        for (int j = 0; j < 1 + int(rng() % 6); ++j) m[16 + rng() % (m.size() - 16)] = uint8_t(rng());
        (void)nm::parse_smsb(m, &err);
    }
}

// as 11 musicas e os 11 bancos reais: as contagens do inventario em Python (n6_smsd.py, n6_smpb.py)
BS_TEST(audio_manatee_real_disc) {
    const char* gd = std::getenv("BS_GAME_DATA");
    if (!gd) BS_SKIP("BS_GAME_DATA ausente");
    const std::map<std::string, std::pair<size_t, size_t>> notas = {  // nome -> (eventos, notas)
        {"M10", {624, 580}},  {"M2A", {2641, 2546}}, {"M2B", {3946, 3868}}, {"M2C", {2001, 1926}},
        {"M3", {3663, 3561}}, {"M4", {6591, 6536}},  {"M5", {3505, 3069}},  {"M6", {5283, 4941}},
        {"M7", {4952, 4875}}, {"M9", {4377, 4289}},  {"M99", {7467, 7073}}};
    const std::map<std::string, std::pair<size_t, size_t>> bancos = {  // nome -> (programas, camadas)
        {"M10", {9, 33}}, {"M2A", {11, 39}}, {"M2B", {11, 31}}, {"M2C", {7, 27}}, {"M3", {8, 32}}, {"M4", {12, 41}},
        {"M5", {10, 33}}, {"M6", {9, 32}},   {"M7", {10, 32}},  {"M9", {8, 32}},  {"M99", {13, 42}}};
    for (const auto& [nome, esperado] : notas) {
        std::string err;
        const auto  b = ler(std::filesystem::path(gd) / "BSDATA" / (nome + ".MSB"));
        auto        s = nm::parse_smsb(b, &err);
        BS_CHECK(s && s->size() == 1);
        if (!s) {
            std::printf("  %s: %s\n", nome.c_str(), err.c_str());
            continue;
        }
        size_t n = 0;
        for (const auto& e : (*s)[0].events) n += e.kind == nm::SeqEvent::Kind::Note;
        BS_CHECK((*s)[0].events.size() == esperado.first && n == esperado.second);
        const auto pb = ler(std::filesystem::path(gd) / "BSDATA" / (nome + ".MPB"));
        auto       bk = nm::parse_smpb(pb, &err);
        BS_CHECK(bk && bk->programs.size() == bancos.at(nome).first && bk->velocityCurves.size() == 2);
        if (!bk) {
            std::printf("  %s.MPB: %s\n", nome.c_str(), err.c_str());
            continue;
        }
        size_t camadas = 0;
        for (const auto& p : bk->programs)
            for (const auto& g : p.groups) camadas += g.layers.size();
        BS_CHECK(camadas == bancos.at(nome).second);
    }
}
