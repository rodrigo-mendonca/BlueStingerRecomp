// N6 fase F: o sequenciador do Manatee (native/audio/manatee_seq), contra o algoritmo do driver (MANATEE.DRV) lido no
// codigo ARM7. Os casos separam as leituras possiveis: a referencia 0x81 com N eventos (nao N-1), o laco 0x82 com o
// corpo c+1 vezes, e a contagem inteira do tempo (a nota cai na chamada em que a conta chega a 0).
#include "test.h"

#include "audio/manatee_seq.h"

#include <cstring>
#include <string>
#include <vector>

namespace nm = bs::native::audio::manatee;

namespace {
void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) { std::memcpy(b.data() + o, &v, 4); }
// SMSB de uma sequencia com cabecalho (136, tempo) e os eventos dados; o SMSD comeca em 20
std::vector<uint8_t> smsb(uint32_t tempo, const std::vector<uint8_t>& ev) {
    std::vector<uint8_t> b(32 + ev.size(), 0);
    std::memcpy(b.data(), "SMSB", 4), put32(b, 4, 1), put32(b, 8, uint32_t(b.size())), put32(b, 12, 1), put32(b, 16, 20);
    std::memcpy(b.data() + 20, "SMSD", 4), put32(b, 24, 136), put32(b, 28, tempo);
    std::memcpy(b.data() + 32, ev.data(), ev.size());
    return b;
}
// nota de gate 1 byte no canal 0: tecla, gate, delta
std::vector<uint8_t> nota(uint8_t tecla, uint8_t gate, uint8_t delta) { return {0x00, tecla, 100, gate, delta}; }
struct Execucao {
    std::vector<nm::MusicEvent> ev;
    std::vector<uint8_t>        teclas;  // so as notas ligadas, em ordem
};
Execucao roda(const std::vector<uint8_t>& b, int chamadas) {
    std::string err;
    static std::vector<nm::Sequence> guarda;  // a sequencia tem que viver enquanto o sequenciador
    guarda = *nm::parse_smsb(b, &err);
    nm::Sequencer s(guarda[0]);
    Execucao      r;
    for (int k = 0; k < chamadas && !s.ended(); ++k) s.call(r.ev);
    for (const auto& e : r.ev)
        if (e.kind == nm::MusicEvent::Kind::NoteOn) r.teclas.push_back(e.a);
    return r;
}
std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> l) {
    std::vector<uint8_t> r;
    for (const auto& v : l) r.insert(r.end(), v.begin(), v.end());
    return r;
}
} // namespace

// 0x81 com contagem 3 de volta a um bloco ja tocado de 4 notas (60..63), como nos dados reais (as 1 010 referencias
// apontam para tras, SMSD + deslocamento): toca de novo 60, 61, 62 e volta para a 70 (N = 3; N-1 tocaria so 60 e 61)
BS_TEST(audio_manatee_seq_reference_count) {
    // eventos a partir do SMSD+12 (deslocamento 12): as 4 notas (5 bytes cada), a referencia ao deslocamento 12, 70, fim
    const auto ev = concat({nota(60, 1, 1), nota(61, 1, 1), nota(62, 1, 1), nota(63, 1, 1), {0x81, 0x00, 12, 3},
                            nota(70, 1, 1), {0x83}});
    const auto r  = roda(smsb(480, ev), 400);
    BS_CHECK((r.teclas == std::vector<uint8_t>{60, 61, 62, 63, 60, 61, 62, 70}));
    if (r.teclas != std::vector<uint8_t>{60, 61, 62, 63, 60, 61, 62, 70}) {
        std::printf("  tocou:");
        for (auto t : r.teclas) std::printf(" %u", t);
        std::printf("\n");
    }
}

// 0x82: o primeiro guarda o ponto, o segundo volta; contador 2 = corpo 3 vezes; 0 = sem fim
BS_TEST(audio_manatee_seq_loop) {
    const auto dois = concat({{0x82, 2, 1}, nota(50, 1, 1), {0x82, 0, 1}, nota(90, 1, 1), {0x83}});
    BS_CHECK((roda(smsb(480, dois), 400).teclas == std::vector<uint8_t>{50, 50, 50, 90}));
    const auto sempre = concat({{0x82, 0, 1}, nota(50, 1, 1), {0x82, 0, 1}, {0x83}});
    BS_CHECK(roda(smsb(480, sempre), 400).teclas.size() > 10);  // nunca acaba
}

// tempo, pela conta do driver: a 1a chamada ja tira 16384 da contagem (0 -> -16384, 0x4354) antes de ler a 1a nota, e
// o resto negativo entra no delta e no gate. unidade = 136 * 480 >> 4 = 4080; delta 100 -> 408000 - 16384 = 391616 ->
// mais 24 chamadas: a 2a nota na chamada 25; gate 20 -> 81600 - 16384 = 65216 -> desliga na chamada 5. Com o 0x84 de
// tempo 960 (unidade 8160) na chamada 1: delta 100 -> 816000 - 16384 = 799616 -> 49 chamadas entre as duas notas
BS_TEST(audio_manatee_seq_integer_timing) {
    const auto r = roda(smsb(480, concat({nota(60, 20, 100), nota(61, 1, 1), {0x83}})), 100);
    std::vector<uint64_t> on, off;
    for (const auto& e : r.ev) {
        if (e.kind == nm::MusicEvent::Kind::NoteOn) on.push_back(e.call);
        if (e.kind == nm::MusicEvent::Kind::NoteOff && e.a == 60) off.push_back(e.call);
    }
    BS_CHECK(on.size() == 2 && on[0] == 1 && on[1] == 25);
    BS_CHECK(off.size() == 1 && off[0] == 5);
    const auto t = roda(smsb(480, concat({{0x84, 0x03, 0xC0, 0}, nota(60, 1, 100), nota(61, 1, 1), {0x83}})), 200);
    std::vector<uint64_t> on2;
    for (const auto& e : t.ev)
        if (e.kind == nm::MusicEvent::Kind::NoteOn) on2.push_back(e.call);
    BS_CHECK(on2.size() == 2 && on2[1] - on2[0] == 49);
}

// Revisao Codex 2026-10-20 (MEDIA): cabecalho de tempo fora da aritmetica do driver e recusado na leitura; a lista de
// notas tem os 48 lugares do driver (0x10300): a 49a nota longa desliga a mais antiga.
BS_TEST(audio_manatee_seq_limits) {
    std::string err;
    BS_CHECK(!nm::parse_smsb(smsb(0x10000, nota(60, 1, 1)), &err));  // tempo de 3 bytes: o 0x84 so tem 2
    auto b = smsb(480, nota(60, 1, 1));
    std::memcpy(b.data() + 24, "\x00\x00\x01\x00", 4);  // cabecalho1 = 0x10000
    BS_CHECK(!nm::parse_smsb(b, &err));
    std::vector<uint8_t> ev;
    for (int k = 0; k < 49; ++k) {  // 49 notas de gate 255 sem delta
        const uint8_t n[] = {0x00, uint8_t(30 + k), 100, 255, 0};
        ev.insert(ev.end(), n, n + 5);
    }
    ev.push_back(0x83);
    static std::vector<nm::Sequence> guarda;
    guarda = *nm::parse_smsb(smsb(480, ev), &err);
    nm::Sequencer               s(guarda[0]);
    std::vector<nm::MusicEvent> out;
    s.call(out);
    size_t on = 0, off = 0;
    for (const auto& e : out) on += e.kind == nm::MusicEvent::Kind::NoteOn, off += e.kind == nm::MusicEvent::Kind::NoteOff;
    BS_CHECK(on == 49 && off == 1 && s.stolen() == 1);
    BS_CHECK(out.size() > 1 && out[0].kind == nm::MusicEvent::Kind::NoteOn && out[0].a == 30);  // a 1a ligou primeiro
}
