// N7c (docs/N7-PLANO.md secao 23): camera livre. O analogico direito e o R3 chegam ao InputSystem como eixos e botao
// proprios (o jogo nunca os ve) e as opcoes ficam na secao [camera] do controles.toml.
#include "test.h"

#include "free_camera.h"
#include "input_bridge.h"
#include "platform/camera_settings.h"
#include "platform/input.h"

#include <cfenv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ni = bs::native::input;
namespace ib = bs::transitional::input_bridge;
namespace st = bs::native::settings;
namespace fs = std::filesystem;

// Padrao: CAMERA_X/Y no analogico direito e o R3 recentraliza; o pad do console nao recebe nada disso.
BS_TEST(camera_input_right_stick_is_not_seen_by_game) {
    ni::InputSystem sys;
    sys.gamepad_added(1, "pad");
    sys.gamepad_axis(1, "rightx", 0.75f);
    sys.gamepad_axis(1, "righty", -0.5f);
    sys.gamepad_button(1, "rightstick", true);
    const ni::ControllerState p = sys.peek();
    BS_CHECK(std::fabs(p.axis(ni::Axis::CameraX) - 0.75f) < 1e-6f && std::fabs(p.axis(ni::Axis::CameraY) + 0.5f) < 1e-6f);
    BS_CHECK((p.buttons & ni::CameraRecenter) != 0);
    const ni::ControllerState g = sys.latch({});
    BS_CHECK(g.axis(ni::Axis::Stick2X) == 0 && g.axis(ni::Axis::Stick2Y) == 0);  // o segundo analogico do console: sem ligacao
    const auto pad = ib::to_pad(g);
    const auto neutral = ib::to_pad(ni::ControllerState{});
    BS_CHECK(pad.buttons == neutral.buttons && pad.joy2x == neutral.joy2x && pad.joy2y == neutral.joy2y);
    // a camera nao usa a zona morta dos controles (tem a sua)
    auto m = ni::ActionMap::parse("[opcoes]\nzona_morta = 0.3\n", nullptr);
    BS_CHECK(m);
    ni::InputSystem z(m.value());
    z.gamepad_added(1, "pad");
    z.gamepad_axis(1, "rightx", 0.2f);
    BS_CHECK(std::fabs(z.peek().axis(ni::Axis::CameraX) - 0.2f) < 1e-6f);
    // com a fonte de teste (roteiro), o jogo so ve o roteiro e a camera continua no controle do host
    z.set_test_source([](const ni::SampleContext&) { return ni::ControllerState{}; });
    BS_CHECK(std::fabs(z.peek().axis(ni::Axis::CameraX) - 0.2f) < 1e-6f && z.latch({}).axis(ni::Axis::CameraX) == 0);
}

BS_TEST(camera_input_remap_and_section) {
    std::string err;
    auto m = ni::ActionMap::parse("[controle]\nCAMERA_X = \"-leftx\"\nCAMERA_CENTRALIZAR = \"y\"\n[camera]\nmodo = \"livre\"\n", &err);
    BS_CHECK(m);
    BS_CHECK(!ni::ActionMap::parse("camera = 1\n", &err));                      // [camera] deve ser secao
    BS_CHECK(!ni::ActionMap::parse("[controle]\nCAMERA_X = \"a\"\n", &err));   // botao num eixo
    BS_CHECK(ni::ActionMap::parse(ni::ActionMap::default_text() + "\n" + st::camera_to_toml({}), &err));
}

// Os testes do SH-4 deixam neste processo o arredondamento do guest (para zero): o menu le e grava na thread da janela,
// com o padrao do host (mais proximo).
struct NearestRounding {
    int saved = std::fegetround();
    NearestRounding() { std::fesetround(FE_TONEAREST); }
    ~NearestRounding() { std::fesetround(saved); }
};

BS_TEST(camera_settings_toml) {
    NearestRounding    rounding;
    BS_CHECK(st::camera_from_toml("") == st::CameraSettings{});
    std::string w;
    const st::CameraSettings s = st::camera_from_toml(
        "[camera]\nmodo = \"hibrida\"\nsensibilidade_x = 1000\nzona_morta = -1\ninverter_y = true\nretorno_s = 5\n"
        "pitch_cima = 10\n", &w);
    BS_CHECK(s.mode == st::CameraMode::Hybrid && s.sens_x == 360 && s.deadzone == 0 && s.invert_y && s.return_s == 5 &&
             s.pitch_up == 10 && w.empty());
    BS_CHECK(st::camera_from_toml("[camera]\nmodo = \"orbita\"\n", &w).mode == st::CameraMode::Original && !w.empty());
    st::CameraSettings a;
    a.mode = st::CameraMode::Free, a.sens_y = 55.5f, a.smoothing = 0.25f, a.r3_recenter = false, a.pitch_down = 7;
    BS_CHECK(st::camera_from_toml(st::camera_to_toml(a)) == a);
    st::CameraSettings nan;
    nan.sens_x = std::nanf("");
    BS_CHECK(st::clamp_camera(nan).sens_x == st::CameraSettings{}.sens_x);
}

// Trocar a secao [camera] nao mexe em nada fora dela (mapas, perfis, comentarios), com ou sem secao depois.
BS_TEST(camera_settings_splice_preserves_rest) {
    NearestRounding    rounding;
    st::CameraSettings c;
    c.mode = st::CameraMode::Free;
    const std::string head = "# meus controles\r\nversao = 1\r\n[teclado]\r\nA = \"SPACE\"  # pulo\r\n";
    const std::string tail = "[perfis.xinput]\r\nSTART = \"back\"\r\n";
    const std::string t1 = st::replace_camera_section(head, c);
    BS_CHECK(t1.rfind(head, 0) == 0 && st::camera_from_toml(t1).mode == st::CameraMode::Free);
    const std::string mid = head + "[camera]\r\nmodo = \"hibrida\"\r\nlixo_antigo = 3\r\n\r\n" + tail;
    const std::string t2 = st::replace_camera_section(mid, c);
    BS_CHECK(t2.rfind(head, 0) == 0 && t2.size() >= tail.size() && t2.compare(t2.size() - tail.size(), tail.size(), tail) == 0);
    BS_CHECK(t2.find("lixo_antigo") == std::string::npos && st::camera_from_toml(t2).mode == st::CameraMode::Free);
    BS_CHECK(ni::ActionMap::parse(t2, nullptr));
    // repetir nao acumula
    BS_CHECK(st::replace_camera_section(t2, c) == t2);
    // gravar e ler pelo disco
    const fs::path dir = fs::temp_directory_path() / "bs_camera_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    { std::ofstream(dir / "controles.toml", std::ios::binary) << mid; }
    BS_CHECK(st::write_camera(dir, c));
    BS_CHECK(st::read_camera(dir) == c);
    std::stringstream ss;
    ss << std::ifstream(dir / "controles.toml", std::ios::binary).rdbuf();
    BS_CHECK(ss.str() == t2);
    fs::remove_all(dir);
    BS_CHECK(st::read_camera(dir) == st::CameraSettings{});
}

// Rig da camera livre: um passo por VBlank.
namespace {
namespace fc = bs::transitional::free_camera;
fc::CameraRig rig(st::CameraMode mode, float smoothing = 0) {
    st::CameraSettings s;
    s.mode = mode, s.smoothing = smoothing, s.sens_x = 90, s.sens_y = 60, s.return_s = 1, s.pitch_up = 20, s.pitch_down = 10;
    fc::CameraRig r;
    r.configure(s);
    return r;
}
void run(fc::CameraRig& r, int n, fc::RigInput in = {}, bool suspended = false) {
    for (int i = 0; i < n; ++i) r.step(in, suspended);
}
} // namespace

BS_TEST(camera_rig_modes) {
    // Original: nada se mexe
    fc::CameraRig o = rig(st::CameraMode::Original);
    run(o, 60, {1, 0});
    BS_CHECK(o.yaw() == 0 && o.pitch() == 0);
    // Livre: 90 graus/s por 1 s; o deslocamento fica depois de soltar
    fc::CameraRig f = rig(st::CameraMode::Free);
    run(f, 60, {1, 0});
    BS_CHECK(std::fabs(f.yaw() - 90 * 60 * fc::CameraRig::kStep) < 0.01f);
    const float kept = f.yaw();
    run(f, 600);
    BS_CHECK(f.yaw() == kept);
    // R3: volta suave ao centro (nao na hora), e nova entrada cancela
    f.step({0, 0, true}, false);
    BS_CHECK(f.yaw() > 0 && f.yaw() < kept && f.returning());
    run(f, 300);
    BS_CHECK(f.yaw() == 0 && !f.returning());
    run(f, 30, {1, 0});
    f.step({0, 0, true}, false);
    f.step({1, 0}, false);
    BS_CHECK(!f.returning());
    // Hibrida: volta sozinha depois de return_s (1 s) sem usar
    fc::CameraRig h = rig(st::CameraMode::Hybrid);
    run(h, 30, {-1, 0});
    const float left = h.yaw();
    BS_CHECK(left < 0);
    run(h, 50);
    BS_CHECK(h.yaw() == left);  // ainda dentro de return_s
    run(h, 300);
    BS_CHECK(h.yaw() == 0);
    // suspensao (cena roteirizada): zera na hora
    run(h, 30, {1, 0});
    h.step({1, 0}, true);
    BS_CHECK(h.yaw() == 0 && h.pitch() == 0);
}

BS_TEST(camera_rig_limits_deadzone_smoothing) {
    fc::CameraRig r = rig(st::CameraMode::Free);
    run(r, 600, {0, -1});  // analogico para cima: olha para cima, ate o limite
    BS_CHECK(r.pitch() == 20);
    run(r, 600, {0, 1});
    BS_CHECK(r.pitch() == -10);
    run(r, 60, {0.1f, 0.1f});  // dentro da zona morta padrao (0,15)
    BS_CHECK(r.yaw() == 0);
    run(r, 10000, {1, 0});  // o yaw da a volta e fica em (-180, 180]
    BS_CHECK(r.yaw() > -180.001f && r.yaw() <= 180.001f);
    // com suavizacao, o primeiro passo anda menos que sem
    fc::CameraRig a = rig(st::CameraMode::Free, 0), b = rig(st::CameraMode::Free, 0.8f);
    a.step({1, 0}, false), b.step({1, 0}, false);
    BS_CHECK(b.yaw() > 0 && b.yaw() < a.yaw());
    // entrada invalida nao propaga
    fc::CameraRig n = rig(st::CameraMode::Free);
    n.step({std::nanf(""), INFINITY}, false);
    BS_CHECK(n.yaw() == 0 && n.pitch() == 0);
}

BS_TEST(camera_orbit_geometry) {
    // o vetor de visao inicial do SDK (8C1740DC) e (0,0,-1); pitch e yaw como 8C174148/8C174180
    const fc::Vec3 f0 = fc::forward(0, 0);
    BS_CHECK(f0.x == 0 && f0.y == 0 && f0.z == -1);
    const fc::Vec3 fy = fc::forward(0x4000, 0);  // 90 graus de yaw
    BS_CHECK(std::fabs(fy.x + 1) < 1e-5f && std::fabs(fy.z) < 1e-5f);
    BS_CHECK(fc::units(90) == 0x4000 && fc::units(-45) == -0x2000);
    const fc::Vec3 pos{10, 50, 200}, player{10, 20, 100};
    // sem deslocamento, a mesma posicao
    const fc::Vec3 same = fc::orbit(pos, 0, 0, player, 0, 0);
    BS_CHECK(std::fabs(same.x - pos.x) < 1e-4f && std::fabs(same.y - pos.y) < 1e-4f && std::fabs(same.z - pos.z) < 1e-4f);
    // orbita em volta do ponto da linha de visao mais perto do jogador: a distancia a ele se mantem
    const fc::Vec3 o = fc::orbit(pos, 0, 0, player, fc::units(60), 0);
    const fc::Vec3 pivot{10, 50, 100};
    const float    d0 = std::hypot(pos.x - pivot.x, pos.y - pivot.y, pos.z - pivot.z);
    const float    d1 = std::hypot(o.x - pivot.x, o.y - pivot.y, o.z - pivot.z);
    BS_CHECK(std::fabs(d0 - d1) < 1e-3f && std::fabs(o.y - pos.y) < 1e-4f);
    // jogador atras da camera: gira no lugar
    const fc::Vec3 behind = fc::orbit(pos, 0, 0, {10, 50, 400}, fc::units(60), fc::units(10));
    BS_CHECK(behind.x == pos.x && behind.y == pos.y && behind.z == pos.z);
    // distancia limitada
    const fc::Vec3 far = fc::orbit({0, 0, 0}, 0, 0, {0, 0, -5000}, fc::units(180), 0);
    BS_CHECK(std::fabs(far.z + 2 * fc::kMaxOrbit) < 1e-2f);
}
