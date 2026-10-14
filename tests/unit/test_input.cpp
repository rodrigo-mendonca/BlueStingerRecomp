// Entrada do jogador (runtime/input) e a traducao para o Maple.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "input/input.h"
#include "maple/maple.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bs;

namespace {

template <class F> bool throws(F&& fn) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

} // namespace

BS_TEST(input_button_names) {
    BS_CHECK(pad_button_from_name("START") == kPadStart && pad_button_from_name("start") == kPadStart);
    BS_CHECK(pad_button_from_name("A") == kPadA && pad_button_from_name("RIGHT") == kPadRight);
    BS_CHECK(pad_button_from_name("L2") == 0);
}

// Roteiro: o estado vale a partir da leitura indicada ate a proxima linha.
BS_TEST(input_script_is_deterministic_by_read_sequence) {
    auto s = make_scripted_input("# comentario\n0 -\n3 START\n5 A+UP   # dois botoes\n7 -\n");
    BS_CHECK(s->read(0, 0).buttons == 0 && s->read(0, 2).buttons == 0);
    BS_CHECK(s->read(0, 3).buttons == kPadStart && s->read(0, 4).buttons == kPadStart);
    BS_CHECK(s->read(0, 5).buttons == (kPadA | kPadUp));
    BS_CHECK(s->read(0, 7).buttons == 0 && s->read(0, 1000).buttons == 0);
    BS_CHECK(s->read(1, 4).buttons == 0);  // so a porta 0
    const PadState n = s->read(0, 0);
    BS_CHECK(n.joyx == 0x80 && n.joyy == 0x80 && n.ltrig == 0 && n.rtrig == 0);
}

BS_TEST(input_script_and_keymap_errors) {
    BS_CHECK(throws([] { make_scripted_input("10 START\n5 A\n"); }));  // fora de ordem
    BS_CHECK(throws([] { make_scripted_input("1 TURBO\n"); }));        // botao desconhecido
    BS_CHECK(throws([] { make_scripted_input("START\n"); }));          // sem numero
    BS_CHECK(throws([] { make_keyboard_input("START ENTER\n"); }));    // sem '='
    BS_CHECK(throws([] { make_keyboard_input("START = TECLA\n"); }));  // tecla desconhecida
    BS_CHECK(!throws([] { make_keyboard_input(""); }));                // padrao
    BS_CHECK(!throws([] { make_keyboard_input("START = SPACE\nA = F1\nLT = 1\n"); }));
}

// O Maple pede a condicao do controle da porta A: com roteiro, a 2a leitura (seq 1) ja
// ve START apertado (bit 3 em 0 no barramento, que e ativo em 0).
BS_TEST(input_script_drives_maple_get_condition) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    holly_intc_reset();
    holly_sb_reset();
    input_set_source(make_scripted_input("1 START\n"));
    sh4_write32(&c, 0xA05F6C14, 1);           // SB_MDEN
    sh4_write32(&c, 0xA05F6C04, 0x0C100000);  // SB_MDSTAR
    sh4_write32(&c, 0xAC100000, 0x80000001);  // ultima, porta 0, 1 palavra
    sh4_write32(&c, 0xAC100004, 0x0C200000);
    sh4_write32(&c, 0xAC100008, 0x01002009);  // condicao, destino 0x20
    sh4_write32(&c, 0xAC10000C, 0x01000000);  // funcao: controle
    uint32_t seen[2];
    for (uint32_t& v : seen) {
        sh4_write32(&c, 0xA05F6C18, 1);
        v = sh4_read32(&c, 0xAC200008);
        c.cycles += 1000000;
        sh4_read32(&c, 0xA05F6900);  // fim do DMA visto (ISTNRM)
    }
    input_set_source(nullptr);
    BS_CHECK(seen[0] == 0x0000FFFF);           // nada apertado
    BS_CHECK(seen[1] == (0x0000FFFFu & ~0x8u));  // START
}

namespace {

// Fonte fixa para conferir a serializacao de gatilhos e analogico.
class FixedInput final : public InputSource {
public:
    PadState s;
    PadState read(int, uint64_t) override { return s; }
};

// Pede a condicao do controle da porta A (destino `dst`) e devolve as 3 palavras de dados.
void getCondition(Sh4Context& c, uint32_t dst, uint32_t out[3]) {
    sh4_write32(&c, 0xA05F6C14, 1);
    sh4_write32(&c, 0xA05F6C04, 0x0C100000);
    sh4_write32(&c, 0xAC100000, 0x80000001);
    sh4_write32(&c, 0xAC100004, 0x0C200000);
    sh4_write32(&c, 0xAC100008, 0x01000009 | (dst << 8));
    sh4_write32(&c, 0xAC10000C, 0x01000000);
    sh4_write32(&c, 0xAC200000, 0);
    sh4_write32(&c, 0xA05F6C18, 1);
    for (int i = 0; i < 3; ++i) out[i] = sh4_read32(&c, 0xAC200004 + 4 * i);
    c.cycles += 1000000;
    sh4_read32(&c, 0xA05F6900);  // fim do DMA visto
}

struct MapleInputHw {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    MapleInputHw() {
        c.ram = ram.data();
        holly_intc_reset();
        holly_sb_reset();
    }
    ~MapleInputHw() { input_set_source(nullptr); }
};

} // namespace

// Os 12 botoes, cada um no seu bit (C, B, A, START, UP, DOWN, LEFT, RIGHT, Z, Y, X, D,
// bits 0-11, ativos em 0), e a ordem dos bytes: botoes, R, L, X, Y, X2, Y2
// (mc.pp.se/dc/controller.html; revisao Codex 2026-10-14).
BS_TEST(input_maple_serializes_every_button_and_axis) {
    MapleInputHw h;
    const char* names[12] = {"C", "B", "A", "START", "UP", "DOWN", "LEFT", "RIGHT", "Z", "Y", "X", "D"};
    std::string script;
    for (int b = 0; b < 12; ++b) script += std::to_string(b) + " " + names[b] + "\n";
    input_set_source(make_scripted_input(script));
    bool ok = true;
    for (int b = 0; b < 12; ++b) {
        uint32_t w[3];
        getCondition(h.c, 0x20, w);
        ok = ok && w[0] == 0x01000000 && (w[1] & 0xFFFF) == (0xFFFFu & ~(1u << b));
    }
    BS_CHECK(ok);
    auto fixed      = std::make_unique<FixedInput>();
    fixed->s.rtrig  = 0x12;
    fixed->s.ltrig  = 0xA5;
    fixed->s.joyx   = 0x23;
    fixed->s.joyy   = 0x67;
    input_set_source(std::move(fixed));
    MapleControllerState second;  // segundo analogico vem do estado fixo do Maple
    second.joy2x = 0x89;
    second.joy2y = 0xCD;
    maple_set_controller(second);
    uint32_t w[3];
    getCondition(h.c, 0x20, w);
    BS_CHECK(w[1] == (0x0000FFFFu | (0x12u << 16) | (0xA5u << 24)));
    BS_CHECK(w[2] == (0x23u | (0x67u << 8) | (0x89u << 16) | (0xCDu << 24)));
    maple_set_controller({});
}

// Descritor da porta A dirigido ao endereco do dispositivo principal da porta B (0x60):
// ninguem responde.
BS_TEST(maple_main_device_only_at_own_port_address) {
    MapleInputHw h;
    sh4_write32(&h.c, 0xA05F6C14, 1);
    sh4_write32(&h.c, 0xA05F6C04, 0x0C100000);
    sh4_write32(&h.c, 0xAC100000, 0x80000000);  // ultima, porta 0
    sh4_write32(&h.c, 0xAC100004, 0x0C200000);
    sh4_write32(&h.c, 0xAC100008, 0x00006001);  // info, destino 0x60
    sh4_write32(&h.c, 0xA05F6C18, 1);
    BS_CHECK(sh4_read32(&h.c, 0xAC200000) == 0xFFFFFFFF);
}
