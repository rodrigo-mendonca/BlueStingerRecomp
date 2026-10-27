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

// N8.4: o barramento do produto e o servico sem dispositivos; estes testes exercitam o modelo do Maple (validacao)
struct ModelBus {
    ModelBus() { periph_use(maple_bus_backend()); }
    ~ModelBus() { periph_use(nullptr); }
};

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

// Roteiro "indice passo" (gameplay assistido): o estado vem do passo de jogo informado pelo host,
// nao do numero da leitura; gatilhos e analogico por nome; texto <-> estado sem perda.
namespace {
uint64_t g_testStep = 0;
}
BS_TEST(input_script_indexed_by_game_step) {
    auto s = make_scripted_input("indice passo\n10 START\n12 UP+RT+ANALOG_LEFT\n14 -\n");
    BS_CHECK(throws([&] { s->read(0, 0); }));  // sem contador de passos: erro, nunca o indice de leitura
    input_set_step_counter([] { return g_testStep; });
    g_testStep = 9;
    BS_CHECK(s->read(0, 500).buttons == 0);  // o numero da leitura nao importa
    g_testStep = 10;
    BS_CHECK(s->read(0, 0).buttons == kPadStart);
    g_testStep = 13;
    const PadState m = s->read(0, 7);
    BS_CHECK(m.buttons == kPadUp && m.rtrig == 255 && m.ltrig == 0 && m.joyx == 0 && m.joyy == 0x80);
    g_testStep = 14;
    BS_CHECK(s->read(0, 7).buttons == 0);
    // roteiro ao vivo: mesma semantica; o mesmo passo substitui, passo anterior e erro
    auto live = make_live_script_input();
    live->push(5, pad_state_from_text("A"));
    live->push(5, pad_state_from_text("B"));
    BS_CHECK(throws([&] { live->push(4, {}); }));
    g_testStep = 4;
    BS_CHECK(live->read(0, 0).buttons == 0);
    g_testStep = 5;
    BS_CHECK(live->read(0, 0).buttons == kPadB);
    input_set_step_counter(nullptr);
    // texto <-> estado (subconjunto digital: gatilhos e analogico so nos extremos)
    BS_CHECK(pad_state_text(pad_state_from_text("up+a+LT+ANALOG_DOWN")) == "A+UP+LT+ANALOG_DOWN");
    BS_CHECK(pad_state_text(pad_state_from_text("-")) == "-");
    BS_CHECK(throws([] { pad_state_from_text("TURBO"); }));
    BS_CHECK(throws([] { make_scripted_input("5 A\nindice passo\n"); }));  // indice depois dos passos
    BS_CHECK(throws([] { make_scripted_input("indice quadro\n"); }));  // indice desconhecido
}

// N3-0: valores exatos de eixos e gatilhos, presenca roteirizada e indice da transicao injetada.
BS_TEST(input_exact_values_presence_and_transition_index) {
    const PadState v = pad_state_from_text("A+AX=79+AY=176+LT=127+RT=128");
    BS_CHECK(v.buttons == kPadA && v.joyx == 79 && v.joyy == 176 && v.ltrig == 127 && v.rtrig == 128 && v.connected);
    BS_CHECK(pad_state_text(v) == "A+LT=127+RT=128+AY=176+AX=79");
    BS_CHECK(pad_state_from_text(pad_state_text(v)).joyx == 79);
    BS_CHECK(pad_state_text(pad_state_from_text("AX=0+AY=255+LT=255")) == "LT+ANALOG_DOWN+ANALOG_LEFT");
    BS_CHECK(pad_state_text(pad_state_from_text("AX=128+LT=0")) == "-");  // centro e solto
    const PadState d = pad_state_from_text("desconectado");
    BS_CHECK(!d.connected && pad_state_text(d) == "DESCONECTADO");
    BS_CHECK(throws([] { pad_state_from_text("AX=256"); }));
    BS_CHECK(throws([] { pad_state_from_text("AX="); }));
    BS_CHECK(throws([] { pad_state_from_text("AZ=3"); }));
    BS_CHECK(throws([] { pad_state_from_text("A+DESCONECTADO"); }));  // so sozinho
    BS_CHECK(pad_state_text(pad_state_from_text("A2X=3+A2Y=250")) == "A2X=3+A2Y=250");
    const uint64_t base = input_transition_index();
    // o indice e absoluto: o roteiro de teste comeca do indice atual
    auto rel = make_scripted_input("indice transicao\n" + std::to_string(base) + " -\n" + std::to_string(base + 2) + " B\n" +
                                   std::to_string(base + 3) + " DESCONECTADO\n");
    BS_CHECK(rel->read(0, 99).buttons == 0);
    input_transition_tick();
    input_transition_tick();
    BS_CHECK(rel->read(0, 0).buttons == kPadB);
    input_transition_tick();
    BS_CHECK(!rel->read(0, 0).connected);
}

// O Maple pede a condicao do controle da porta A: com roteiro, a 2a leitura (seq 1) ja
// ve START apertado (bit 3 em 0 no barramento, que e ativo em 0).
BS_TEST(input_script_drives_maple_get_condition) {
    ModelBus bus;
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
    ModelBus bus;
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
    fixed->s.joy2x  = 0x89;  // segundo analogico (N3-0: vem da fonte, como o primeiro)
    fixed->s.joy2y  = 0xCD;
    input_set_source(std::move(fixed));
    uint32_t w[3];
    getCondition(h.c, 0x20, w);
    BS_CHECK(w[1] == (0x0000FFFFu | (0x12u << 16) | (0xA5u << 24)));
    BS_CHECK(w[2] == (0x23u | (0x67u << 8) | (0x89u << 16) | (0xCDu << 24)));
    maple_set_controller({});
}

// Descritor da porta A dirigido ao endereco do dispositivo principal da porta B (0x60):
// ninguem responde.
BS_TEST(maple_main_device_only_at_own_port_address) {
    ModelBus bus;
    MapleInputHw h;
    sh4_write32(&h.c, 0xA05F6C14, 1);
    sh4_write32(&h.c, 0xA05F6C04, 0x0C100000);
    sh4_write32(&h.c, 0xAC100000, 0x80000000);  // ultima, porta 0
    sh4_write32(&h.c, 0xAC100004, 0x0C200000);
    sh4_write32(&h.c, 0xAC100008, 0x00006001);  // info, destino 0x60
    sh4_write32(&h.c, 0xA05F6C18, 1);
    BS_CHECK(sh4_read32(&h.c, 0xAC200000) == 0xFFFFFFFF);
}

// N3-0: um estado DESCONECTADO deixa a porta A vazia (DEVICE_INFO e GET_CONDITION sem resposta);
// ao reconectar, a porta responde de novo.
BS_TEST(maple_scripted_presence_empties_port) {
    ModelBus bus;
    MapleInputHw h;
    maple_reset();
    auto  owned = std::make_unique<FixedInput>();
    auto* in    = owned.get();
    input_set_source(std::move(owned));
    in->s.buttons = kPadA;
    uint32_t w[3];
    getCondition(h.c, 0x20, w);
    BS_CHECK(w[0] == 0x01000000 && (w[1] & 0xFFFF) == (0xFFFFu & ~uint32_t(kPadA)));
    in->s.connected = false;
    getCondition(h.c, 0x20, w);
    BS_CHECK(sh4_read32(&h.c, 0xAC200000) == 0xFFFFFFFF);  // ninguem responde
    BS_CHECK(maple_cond_seq(0) == 1);                       // porta vazia nao conta leitura
    in->s           = {};
    in->s.buttons   = kPadB;
    getCondition(h.c, 0x20, w);
    BS_CHECK(w[0] == 0x01000000 && (w[1] & 0xFFFF) == (0xFFFFu & ~uint32_t(kPadB)) && maple_cond_seq(0) == 2);
    // roteiro por leitura com DESCONECTADO travaria (porta vazia nao gera leitura): recusado
    BS_CHECK(throws([] { make_scripted_input("0 -\n5 DESCONECTADO\n"); }));
    BS_CHECK(!throws([] { make_scripted_input("indice passo\n0 -\n5 DESCONECTADO\n"); }));
}

// N3-0: a transicao injetada avanca uma vez por VBlank-In, no primeiro acesso ao MDST (o tratador
// do SDK le o MDST antes de disparar o DMA); sem VBlank-In, ler o MDST nao avanca nada.
BS_TEST(maple_transition_ticks_on_first_mdst_read_after_vblank) {
    ModelBus bus;
    MapleInputHw h;
    maple_reset();
    const uint64_t base = input_transition_index();
    sh4_read32(&h.c, 0xA05F6C18);
    BS_CHECK(input_transition_index() == base);
    maple_vblank_in();
    BS_CHECK(input_transition_index() == base);  // a entrega do VBlank-In nao avanca
    sh4_read32(&h.c, 0xA05F6C18);
    sh4_read32(&h.c, 0xA05F6C18);
    BS_CHECK(input_transition_index() == base + 1);
}
