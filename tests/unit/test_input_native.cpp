// N3-1: nucleo nativo de entrada (native/platform/input.h) e a ponte com o legado
// (transitional/input_bridge.h).
#include "test.h"

#include "cpu/sh4_mem.h"
#include "input/input.h"
#include "input_bridge.h"
#include "platform/input.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace bs;
namespace ni = bs::native::input;
namespace ib = bs::transitional::input_bridge;

namespace {

bool bound(const std::vector<ni::Binding>& v, const std::string& control, uint32_t button) {
    for (const auto& b : v)
        if (b.control == control && b.target.kind == ni::Target::Kind::Button && b.target.button == button) return true;
    return false;
}
bool controlUsed(const std::vector<ni::Binding>& v, const std::string& control) {
    for (const auto& b : v)
        if (b.control == control) return true;
    return false;
}

} // namespace

// Conversoes exatas: os 256 bytes de eixo e de gatilho vao e voltam iguais; centro, extremos e
// arredondamento como em docs/N3-PLANO.md 3.1 (decisao 3).
BS_TEST(input_native_conversions_are_exact) {
    bool ok = true;
    for (int b = 0; b < 256; ++b) {
        ok = ok && ib::axis_byte(ib::axis_from_byte(uint8_t(b))) == b;
        ok = ok && ib::trigger_byte(ib::trigger_from_byte(uint8_t(b))) == b;
    }
    BS_CHECK(ok);
    BS_CHECK(ib::axis_byte(0.0f) == 128 && ib::axis_byte(-1.0f) == 0 && ib::axis_byte(1.0f) == 255);
    BS_CHECK(ib::axis_byte(-2.0f) == 0 && ib::axis_byte(2.0f) == 255);  // saturacao
    BS_CHECK(ib::trigger_byte(0.0f) == 0 && ib::trigger_byte(1.0f) == 255 && ib::trigger_byte(0.5f) == 128);
    BS_CHECK(ni::axis_from_s16(-32768) == -1.0f && ni::axis_from_s16(32767) == 1.0f && ni::axis_from_s16(0) == 0.0f);
    BS_CHECK(ni::trigger_from_u8(255) == 1.0f && ni::trigger_from_s16(32767) == 1.0f && ni::trigger_from_s16(-5) == 0.0f);
    // estado completo: botoes (os 12), eixos, segundo analogico, gatilhos, conexao
    PadState p;
    p.buttons   = 0x0FFF;
    p.joyx      = 0, p.joyy = 255, p.joy2x = 7, p.joy2y = 200, p.ltrig = 127, p.rtrig = 128;
    BS_CHECK(ib::to_pad(ib::to_controller(p)).buttons == p.buttons);
    const PadState back = ib::to_pad(ib::to_controller(p));
    BS_CHECK(back.joyx == 0 && back.joyy == 255 && back.joy2x == 7 && back.joy2y == 200 && back.ltrig == 127 &&
             back.rtrig == 128 && back.connected);
    PadState gone;
    gone.connected = false;
    BS_CHECK(!ib::to_pad(ib::to_controller(gone)).connected);
    BS_CHECK(ib::to_controller(PadState{}).buttons == 0 && ib::to_pad(ni::ControllerState{}).joyx == 128);
    // cada botao no seu bit, nos dois sentidos
    const uint16_t pads[]  = {kPadA, kPadB, kPadX, kPadY, kPadStart, kPadUp, kPadDown, kPadLeft, kPadRight};
    const uint32_t nats[]  = {ni::A, ni::B, ni::X, ni::Y, ni::Start, ni::DpadUp, ni::DpadDown, ni::DpadLeft, ni::DpadRight};
    bool           bits_ok = true;
    for (int i = 0; i < 9; ++i) {
        PadState q;
        q.buttons = pads[i];
        bits_ok   = bits_ok && ib::to_controller(q).buttons == nats[i];
    }
    BS_CHECK(bits_ok);
}

// Zona morta radial do host: desligada por padrao (0); ligada, zera dentro e reescala fora.
BS_TEST(input_native_radial_deadzone) {
    float x = 0.1f, y = 0.0f;
    ni::apply_radial_deadzone(x, y, 0.0f);
    BS_CHECK(x == 0.1f);
    ni::apply_radial_deadzone(x, y, 0.2f);
    BS_CHECK(x == 0.0f && y == 0.0f);
    x = 0.6f, y = 0.0f;
    ni::apply_radial_deadzone(x, y, 0.2f);
    BS_CHECK(std::fabs(x - 0.5f) < 1e-6f);
    x = 1.0f, y = 0.0f;
    ni::apply_radial_deadzone(x, y, 0.2f);
    BS_CHECK(std::fabs(x - 1.0f) < 1e-6f);
}

// Mapa padrao, remapeamento por TOML (a acao listada substitui o padrao dela; o resto fica),
// arquivo padrao = mapa padrao, e erros com linha e motivo.
BS_TEST(input_native_action_map_and_toml) {
    const ni::ActionMap d = ni::ActionMap::defaults();
    BS_CHECK(bound(d.keyboard, "Z", ni::A) && bound(d.keyboard, "ENTER", ni::Start) && bound(d.gamepad, "a", ni::A));
    BS_CHECK(d.options.backend == "sdl" && d.options.deadzone == 0.0f && d.options.auto_switch);
    std::string err;
    auto        def = ni::ActionMap::parse(ni::ActionMap::default_text(), &err);
    BS_CHECK(def && def.value().keyboard.size() == d.keyboard.size() && def.value().gamepad.size() == d.gamepad.size());
    auto m = ni::ActionMap::parse("versao = 1\n[opcoes]\nbackend = \"xinput\"\nzona_morta = 0.15\n"
                                  "troca_automatica = false\ncontrole_preferido = \"xbox\"\n"
                                  "[teclado]\nA = [\"space\", \"Z\"]\nB = \"F2\"\n"
                                  "[controle]\nA = \"b\"\nB = \"a\"\nANALOG_Y = \"-lefty\"\n",
                                  &err);
    BS_CHECK(m);
    if (m) {
        const ni::ActionMap& v = m.value();
        BS_CHECK(v.options.backend == "xinput" && std::fabs(v.options.deadzone - 0.15f) < 1e-6f && !v.options.auto_switch &&
                 v.options.preferred == "xbox");
        BS_CHECK(bound(v.keyboard, "SPACE", ni::A) && bound(v.keyboard, "Z", ni::A) && bound(v.keyboard, "F2", ni::B));
        BS_CHECK(!controlUsed(v.keyboard, "X"));            // a tecla X era do B padrao
        BS_CHECK(bound(v.keyboard, "ENTER", ni::Start));   // acao nao listada: continua
        BS_CHECK(bound(v.gamepad, "b", ni::A) && bound(v.gamepad, "a", ni::B));
        bool inverted = false;
        for (const auto& b : v.gamepad)
            if (b.control == "lefty" && b.target.kind == ni::Target::Kind::AxisFull && b.target.sign < 0) inverted = true;
        BS_CHECK(inverted);
    }
    BS_CHECK(!ni::ActionMap::parse("[teclado]\nTURBO = \"Z\"\n", &err) && err.find("TURBO") != std::string::npos);
    BS_CHECK(!ni::ActionMap::parse("[teclado]\nA = \"F13\"\n", &err) && err.find("F13") != std::string::npos);
    BS_CHECK(!ni::ActionMap::parse("[controle]\nANALOG_X = \"a\"\n", &err));      // botao num eixo
    BS_CHECK(!ni::ActionMap::parse("[controle]\nA = \"leftx\"\n", &err));         // eixo num botao
    BS_CHECK(!ni::ActionMap::parse("[opcoes]\nbackend = \"dinput\"\n", &err));
    BS_CHECK(!ni::ActionMap::parse("[opcoes]\nzona_morta = 0.95\n", &err));
    BS_CHECK(!ni::ActionMap::parse("versao = 2\n", &err));
    BS_CHECK(!ni::ActionMap::parse("[mouse]\n", &err));
    BS_CHECK(!ni::ActionMap::parse("[teclado]\nA = \n", &err) && err.find("linha 2") != std::string::npos);
    BS_CHECK(ni::ActionMap::parse("[controle]\nLT = \"leftshoulder\"\nRT = \"righttrigger\"\n", &err));
    // arquivo salvo com BOM UTF-8 (Bloco de Notas antigo, PowerShell 5.1) e com CRLF
    auto bom = ni::ActionMap::parse("\xEF\xBB\xBFversao = 1\r\n[teclado]\r\nA = \"SPACE\"\r\n", &err);
    BS_CHECK(bom && bound(bom.value().keyboard, "SPACE", ni::A));
    // formato antigo do --map: substitui o teclado inteiro
    ni::ActionMap legacy = ni::ActionMap::defaults();
    BS_CHECK(legacy.set_keyboard_legacy("START = SPACE\nA = F1\nLT = 1\n", &err));
    BS_CHECK(legacy.keyboard.size() == 3 && bound(legacy.keyboard, "F1", ni::A) && !controlUsed(legacy.keyboard, "Z"));
    BS_CHECK(!legacy.set_keyboard_legacy("START ENTER\n", &err));
    BS_CHECK(!legacy.set_keyboard_legacy("START = TECLA\n", &err));
}

// Teclado: botoes, gatilhos e analogico no limite; os dois lados apertados: direita/baixo.
BS_TEST(input_native_keyboard) {
    ni::InputSystem sys;
    sys.key("Z", true);
    sys.key("Q", true);
    sys.key("J", true);
    ni::ControllerState s = sys.latch({});
    BS_CHECK(s.buttons == ni::A && s.axis(ni::Axis::TriggerL) == 1.0f && s.axis(ni::Axis::StickX) == -1.0f && s.connected);
    sys.key("L", true);
    BS_CHECK(sys.latch({}).axis(ni::Axis::StickX) == 1.0f);
    sys.release_keyboard();
    s = sys.latch({});
    BS_CHECK(s.buttons == 0 && s.axis(ni::Axis::StickX) == 0.0f && s.axis(ni::Axis::TriggerL) == 0.0f);
    BS_CHECK(ib::to_pad(s).joyx == 128 && ib::to_pad(s).ltrig == 0);
    // perder o foco solta o teclado, nao os controles (o SDL nao reenvia o botao mantido)
    sys.gamepad_added(1, "pad");
    sys.gamepad_button(1, "b", true);
    sys.key("Z", true);
    sys.release_keyboard();
    BS_CHECK(sys.latch({}).buttons == ni::B);
}

// Controles: botoes, eixos, gatilhos, zona morta, conexao/desconexao, troca automatica, preferido.
BS_TEST(input_native_gamepads_and_connection) {
    ni::InputSystem sys;
    BS_CHECK(sys.active_gamepad() == -1);
    BS_CHECK(sys.gamepad_added(10, "Xbox Controller") == 0);
    BS_CHECK(sys.gamepad_added(10, "Xbox Controller") == -1);  // repetido
    sys.gamepad_button(10, "a", true);
    sys.gamepad_axis(10, "leftx", -0.6f);
    sys.gamepad_axis(10, "lefty", 1.0f);
    sys.gamepad_axis(10, "righttrigger", 0.25f);
    ni::ControllerState s = sys.latch({});
    BS_CHECK(s.buttons == ni::A && std::fabs(s.axis(ni::Axis::StickX) + 0.6f) < 1e-6f && s.axis(ni::Axis::StickY) == 1.0f &&
             s.axis(ni::Axis::TriggerR) == 0.25f);
    auto ev = sys.drain_events();
    BS_CHECK(ev.size() == 1 && ev[0].kind == ni::ConnectionEvent::Kind::Connected && ev[0].name == "Xbox Controller");
    // segundo controle: conectar neutro nao toma o jogador 1 (revisao Codex 2026-10-18); assume
    // quando aperta algo (troca automatica)
    sys.gamepad_button(10, "a", true);
    sys.gamepad_added(20, "DualSense Wireless Controller");
    BS_CHECK(sys.active_gamepad() == 0 && (sys.latch({}).buttons & ni::A));
    sys.gamepad_button(20, "y", true);
    BS_CHECK(sys.active_gamepad() == 1);
    sys.gamepad_button(20, "y", false);
    sys.gamepad_button(10, "a", false);  // soltar nao e atividade
    BS_CHECK(sys.active_gamepad() == 1);
    sys.gamepad_button(10, "x", true);
    BS_CHECK(sys.active_gamepad() == 0);
    sys.gamepad_button(10, "x", false);
    sys.gamepad_axis(10, "leftx", 0.0f);
    sys.gamepad_axis(10, "lefty", 0.0f);
    sys.gamepad_axis(10, "righttrigger", 0.0f);
    sys.gamepad_button(20, "y", true);
    BS_CHECK(sys.active_gamepad() == 1 && sys.latch({}).buttons == ni::Y);
    // desconectar o ativo: volta para o outro; a porta continua com o teclado (conectada)
    sys.gamepad_removed(20);
    BS_CHECK(sys.active_gamepad() == 0 && sys.latch({}).connected);
    ev = sys.drain_events();
    BS_CHECK(ev.size() == 2 && ev[1].kind == ni::ConnectionEvent::Kind::Disconnected);
    sys.gamepad_button(20, "a", true);  // id desconectado: ignorado
    BS_CHECK(!(sys.latch({}).buttons & ni::A));
    // reconectar o mesmo id: entra como novo
    BS_CHECK(sys.gamepad_added(20, "DualSense Wireless Controller") == 2);
    // preferido por nome e zona morta do host
    ni::ActionMap m       = ni::ActionMap::defaults();
    m.options.preferred   = "dualsense";
    m.options.auto_switch = true;
    m.options.deadzone    = 0.3f;
    ni::InputSystem p(m);
    p.gamepad_added(1, "Xbox Controller");
    p.gamepad_added(2, "DualSense Wireless Controller");
    p.gamepad_button(1, "a", true);
    BS_CHECK(p.active_gamepad() == 1);
    p.gamepad_axis(2, "leftx", 0.2f);
    BS_CHECK(p.latch({}).axis(ni::Axis::StickX) == 0.0f);
    // teclado e controle juntos: botoes por OU, analogico da primeira fonte fora do neutro (teclado)
    ni::InputSystem k;
    k.gamepad_added(5, "pad");
    k.gamepad_button(5, "start", true);
    k.gamepad_axis(5, "leftx", 0.5f);
    k.key("Z", true);
    BS_CHECK(k.latch({}).buttons == (ni::A | ni::Start) && k.latch({}).axis(ni::Axis::StickX) == 0.5f);
    k.key("J", true);
    BS_CHECK(k.latch({}).axis(ni::Axis::StickX) == -1.0f);
}

// XInput de reserva com leitor injetado: botoes pelos nomes do SDL, y invertido, gatilhos de 8 bits,
// conexao e desconexao por slot, troca automatica tambem por eixo; gatilho ligado a um botao.
BS_TEST(input_native_xinput_fallback) {
    ni::InputSystem::XInputPad slots[4];
    bool                       present[4] = {true, false, false, false};
    ni::InputSystem            sys;
    sys.enable_xinput([&](int slot, ni::InputSystem::XInputPad& out) {
        if (!present[slot]) return false;
        out = slots[slot];
        return true;
    });
    slots[0].buttons      = 0x1000 | 0x0010;  // XINPUT_GAMEPAD_A | START
    slots[0].ly           = 32767;            // para cima no XInput
    slots[0].lx           = -32768;
    slots[0].lt           = 255;
    slots[0].rt           = 128;
    ni::ControllerState s = sys.latch({});
    BS_CHECK(s.buttons == (ni::A | ni::Start) && s.axis(ni::Axis::StickY) == -1.0f && s.axis(ni::Axis::StickX) == -1.0f);
    BS_CHECK(s.axis(ni::Axis::TriggerL) == 1.0f && ib::to_pad(s).rtrig == 128 && ib::to_pad(s).joyy == 0);
    BS_CHECK(sys.active_gamepad() == 0 && sys.drain_events().size() == 1);
    // segundo slot chega neutro: nao toma o jogador 1; assume ao apertar; o primeiro volta ao
    // inclinar um eixo
    present[1] = true;
    slots[0]   = {};
    sys.latch({});
    BS_CHECK(sys.active_gamepad() == 0);
    slots[1].buttons = 0x2000;  // XINPUT_GAMEPAD_B
    BS_CHECK(sys.latch({}).buttons == ni::B && sys.active_gamepad() == 1);
    slots[0].rx = 30000;
    sys.latch({});
    BS_CHECK(sys.active_gamepad() == 0);
    // desconectar o slot ativo: o outro assume; reconectar entra como novo
    present[0] = false;
    sys.latch({});
    BS_CHECK(sys.active_gamepad() == 1);
    auto ev = sys.drain_events();
    BS_CHECK(ev.size() == 2 && ev[1].kind == ni::ConnectionEvent::Kind::Disconnected && ev[1].name == "XInput 1");
    // gatilho no ombro (remapeamento): botao digital vira gatilho no fundo
    std::string err;
    auto        m = ni::ActionMap::parse("[controle]\nLT = \"leftshoulder\"\n", &err);
    BS_CHECK(m);
    ni::InputSystem g(m.value());
    g.gamepad_added(1, "pad");
    g.gamepad_button(1, "leftshoulder", true);
    g.gamepad_axis(1, "lefttrigger", 1.0f);  // o gatilho fisico deixou de ser o LT
    BS_CHECK(ib::to_pad(g.latch({})).ltrig == 255);
    g.gamepad_button(1, "leftshoulder", false);
    BS_CHECK(ib::to_pad(g.latch({})).ltrig == 0);
    // desconectar com botao apertado: solta
    g.gamepad_button(1, "a", true);
    g.gamepad_removed(1);
    BS_CHECK(g.latch({}).buttons == 0 && g.latch({}).connected);
}

// Fonte de teste exclusiva e a ponte: o Maple le a amostra do InputSystem pela fonte do runtime,
// com o numero da leitura; roteiro -> estado logico -> estado do Maple sem perda.
BS_TEST(input_native_bridge_and_test_source) {
    ni::InputSystem sys;
    sys.key("Z", true);  // ignorado com fonte de teste
    std::shared_ptr<InputSource> s2(make_scripted_input("0 -\n3 A+AX=79+LT=127+A2Y=3\n").release());
    sys.set_test_source(ib::from_runtime_source(s2));
    ib::install(&sys);
    BS_CHECK(input_source() != nullptr);
    PadState r = input_source()->read(0, 2);
    BS_CHECK(r.buttons == 0 && r.joyx == 128);
    r = input_source()->read(0, 3);
    BS_CHECK(r.buttons == kPadA && r.joyx == 79 && r.ltrig == 127 && r.joy2y == 3 && r.connected);
    BS_CHECK(ib::to_pad(sys.last()).joyx == 79);
    BS_CHECK(input_source()->read(1, 3).buttons == 0);  // so a porta A
    ib::install(nullptr);
    BS_CHECK(input_source() == nullptr);
}

// Revisao Codex 2026-10-18: o latch so no GET_CONDITION. Num DMA com DEVICE_INFO e GET_CONDITION
// para a porta A, uma amostra (as consultas ao XInput so acontecem no latch); a presenca dos outros
// comandos vem de connected(), que nao amostra. Nomes de botoes do SDL3 fixado no TOML.
BS_TEST(input_native_latch_only_on_get_condition) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    holly_intc_reset();
    holly_sb_reset();
    int             polls = 0;
    ni::InputSystem sys;
    sys.enable_xinput([&](int slot, ni::InputSystem::XInputPad& out) {
        if (slot == 0) ++polls;
        out.buttons = 0x1000;  // A
        return slot == 0;
    });
    ib::install(&sys);
    sh4_write32(&c, 0xA05F6C14, 1);           // SB_MDEN
    sh4_write32(&c, 0xA05F6C04, 0x0C100000);  // SB_MDSTAR
    sh4_write32(&c, 0xAC100000, 0x00000000);  // porta 0, 0 palavras, nao e a ultima
    sh4_write32(&c, 0xAC100004, 0x0C200100);
    sh4_write32(&c, 0xAC100008, 0x00002001);  // DEVICE_INFO, destino 0x20
    sh4_write32(&c, 0xAC10000C, 0x80000001);  // ultima, porta 0, 1 palavra
    sh4_write32(&c, 0xAC100010, 0x0C200000);
    sh4_write32(&c, 0xAC100014, 0x01002009);  // GET_CONDITION
    sh4_write32(&c, 0xAC100018, 0x01000000);  // funcao: controle
    sh4_write32(&c, 0xA05F6C18, 1);
    const uint32_t info = sh4_read32(&c, 0xAC200104), cond = sh4_read32(&c, 0xAC200008);
    c.cycles += 1000000;
    sh4_read32(&c, 0xA05F6900);
    ib::install(nullptr);
    BS_CHECK(info == 0x01000000);                       // DEVICE_INFO respondido (funcao: controle)
    BS_CHECK(cond == (0x0000FFFFu & ~0x4u) && polls == 1);  // A apertado, uma amostra so
    // presenca sem amostra: fonte de teste diz; host sempre presente
    ni::InputSystem t;
    t.set_test_source([](const ni::SampleContext& ctx) {
        ni::ControllerState s;
        s.connected = ctx.sample != 3;
        return s;
    });
    BS_CHECK(t.connected({2}) && !t.connected({3}) && ni::InputSystem().connected({3}));
    std::string err;
    BS_CHECK(ni::ActionMap::parse("[controle]\nA = \"misc2\"\nB = \"misc6\"\nX = \"paddle1\"\n", &err));
    BS_CHECK(!ni::ActionMap::parse("[controle]\nA = \"misc7\"\n", &err));
}

// N3-3: perfis por GUID de dispositivo. O perfil herda do [controle] e substitui so as acoes que
// lista; controle sem perfil (GUID desconhecido ou vazio) usa o [controle]; zona morta por perfil;
// perfil "xinput" para o XInput de reserva; erros com o motivo.
BS_TEST(input_native_profiles_by_guid) {
    const std::string xbox = "030000005e0400008e02000000007200";
    std::string       err;
    auto              m = ni::ActionMap::parse("[controle]\nX = \"y\"\n"
                                               "[perfis.\"030000005E0400008E02000000007200\"]\nnome = \"Xbox 360\"\nzona_morta = 0.5\n"
                                               "A = \"b\"\nB = \"a\"\n"
                                               "[perfis.xinput]\nSTART = \"back\"\n",
                                  &err);
    BS_CHECK(m);
    if (!m) return;
    const ni::ActionMap& v = m.value();
    const ni::Profile*   p = v.profile_for(xbox);
    BS_CHECK(p && p->name == "Xbox 360" && std::fabs(p->deadzone - 0.5f) < 1e-6f);
    BS_CHECK(v.profile_for("030000005E0400008E02000000007200") == p && !v.profile_for("") && !v.profile_for("ffff"));
    if (p) {
        BS_CHECK(bound(p->gamepad, "b", ni::A) && bound(p->gamepad, "a", ni::B));  // do perfil
        BS_CHECK(bound(p->gamepad, "y", ni::X) && bound(p->gamepad, "start", ni::Start));  // herdados do [controle]
    }
    BS_CHECK(v.profile_for("xinput") && bound(v.profile_for("xinput")->gamepad, "back", ni::Start));
    // no InputSystem: perfil pelo GUID; sem perfil, o [controle]
    ni::InputSystem sys(v);
    sys.gamepad_added(1, "Xbox 360 Controller", xbox);
    sys.gamepad_button(1, "a", true);
    sys.gamepad_axis(1, "leftx", 0.4f);  // dentro da zona morta do perfil
    ni::ControllerState s = sys.latch({});
    BS_CHECK(s.buttons == ni::B && s.axis(ni::Axis::StickX) == 0.0f);
    auto ev = sys.drain_events();
    BS_CHECK(ev.size() == 1 && ev[0].guid == xbox && ev[0].profile == "Xbox 360");
    sys.gamepad_removed(1);
    sys.gamepad_added(2, "Generico", "03000000aaaaaaaabbbbbbbb00000000");  // sem perfil
    sys.gamepad_button(2, "a", true);
    sys.gamepad_axis(2, "leftx", 0.4f);
    s = sys.latch({});
    BS_CHECK(s.buttons == ni::A && std::fabs(s.axis(ni::Axis::StickX) - 0.4f) < 1e-6f);
    ev = sys.drain_events();
    BS_CHECK(ev.size() == 2 && ev[1].profile.empty());
    // erros
    BS_CHECK(!ni::ActionMap::parse("[perfis.\"123\"]\nA = \"b\"\n", &err) && err.find("32") != std::string::npos);
    BS_CHECK(!ni::ActionMap::parse("[perfis.\"" + xbox + "\"]\nTURBO = \"b\"\n", &err) && err.find("TURBO") != std::string::npos);
    BS_CHECK(!ni::ActionMap::parse("[perfis.\"" + xbox + "\"]\nzona_morta = 2\n", &err));
    BS_CHECK(!ni::ActionMap::parse("perfis = 3\n", &err));
    BS_CHECK(!ni::ActionMap::parse("[perfis.\"" + xbox + "\"]\nA = \"b\"\n[perfis.\"030000005E0400008E02000000007200\"]\nB = \"a\"\n", &err));
    // o arquivo padrao continua valido e sem perfis
    auto d = ni::ActionMap::parse(ni::ActionMap::default_text(), &err);
    BS_CHECK(d && d.value().profiles.empty());
}

// N3-3 (revisao Codex): perfis com desconexao/reconexao, dois controles com perfis diferentes na troca
// automatica e com o preferido, o perfil no evento de desconexao, e [perfis.xinput] no XInput de reserva.
BS_TEST(input_native_profiles_with_hotplug_and_xinput) {
    const std::string gA = "030000005e0400008e02000000007200", gB = "030000004c050000cc09000000006800";
    std::string       err;
    auto m = ni::ActionMap::parse("[perfis.\"" + gA + "\"]\nnome = \"xbox\"\nA = \"b\"\nB = \"a\"\n"
                                  "[perfis.\"" + gB + "\"]\nnome = \"ps\"\nA = \"x\"\nX = \"a\"\n[perfis.xinput]\nnome = \"xi\"\nSTART = \"back\"\n",
                                  &err);
    BS_CHECK(m);
    if (!m) return;
    {
        ni::InputSystem sys(m.value());
        sys.gamepad_added(1, "Xbox", gA);
        sys.gamepad_added(2, "DualShock", gB);
        sys.gamepad_button(1, "a", true);  // xbox: a -> B
        BS_CHECK(sys.active_gamepad() == 0 && sys.latch({}).buttons == ni::B);
        sys.gamepad_button(2, "x", true);  // ps: x -> A; troca automatica
        BS_CHECK(sys.active_gamepad() == 1 && sys.latch({}).buttons == ni::A);
        // desconectar o ps (com x apertado): volta ao xbox, com o perfil dele; o evento leva o perfil
        sys.gamepad_removed(2);
        BS_CHECK(sys.active_gamepad() == 0 && sys.latch({}).buttons == ni::B);
        auto ev = sys.drain_events();
        BS_CHECK(ev.size() == 3 && ev[2].kind == ni::ConnectionEvent::Kind::Disconnected && ev[2].profile == "ps" &&
                 ev[2].guid == gB);
        // reconectar o mesmo GUID: o perfil volta a valer
        sys.gamepad_added(3, "DualShock", gB);
        sys.gamepad_button(3, "x", true);
        BS_CHECK(sys.active_gamepad() == 2 && sys.latch({}).buttons == ni::A);
        ev = sys.drain_events();
        BS_CHECK(ev.size() == 1 && ev[0].profile == "ps");
    }
    {
        ni::ActionMap pref       = m.value();
        pref.options.preferred   = "xbox";
        ni::InputSystem sys(pref);
        sys.gamepad_added(1, "Xbox One", gA);
        sys.gamepad_added(2, "DualShock", gB);
        sys.gamepad_button(2, "x", true);  // atividade no ps nao tira o preferido
        sys.gamepad_button(1, "a", true);
        BS_CHECK(sys.active_gamepad() == 0 && sys.latch({}).buttons == ni::B);
    }
    {
        ni::InputSystem sys(m.value());
        sys.enable_xinput([](int slot, ni::InputSystem::XInputPad& out) {
            out.buttons = 0x0020;  // XINPUT_GAMEPAD_BACK
            return slot == 0;
        });
        BS_CHECK(sys.latch({}).buttons == ni::Start);  // [perfis.xinput]: START = back
        auto ev = sys.drain_events();
        BS_CHECK(ev.size() == 1 && ev[0].guid == "xinput" && ev[0].profile == "xi");
    }
}
