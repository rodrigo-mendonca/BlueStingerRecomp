#pragma once
// Controles (N3-1, docs/N3-PLANO.md). Nucleo nativo de entrada: dispositivos do host (teclado da
// janela, controles pelo SDL3 Gamepad, XInput de reserva) e fontes de teste viram um estado logico
// por jogador, congelado no ponto de amostragem (latch). Nada aqui conhece o protocolo do console:
// quem traduz o estado para o jogo e o adaptador da fase hibrida (transitional/).
//
// Convencoes do estado logico:
//   - botoes: Button (bits proprios, nao os de nenhum protocolo);
//   - analogicos em [-1, 1], x positivo = direita, y positivo = baixo;
//   - gatilhos em [0, 1];
//   - connected = false: a porta do jogador fica vazia (o jogo ve "sem controle").
#include "core/result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace bs::native::input {

enum Button : uint32_t {
    A         = 1u << 0,
    B         = 1u << 1,
    X         = 1u << 2,
    Y         = 1u << 3,
    Start     = 1u << 4,
    DpadUp    = 1u << 5,
    DpadDown  = 1u << 6,
    DpadLeft  = 1u << 7,
    DpadRight = 1u << 8,
    C         = 1u << 9,   // botoes extras que o controle original suporta e o padrao nao tem
    Z         = 1u << 10,
    D         = 1u << 11,
    // N7c: so da camera livre (o jogo nunca ve: o adaptador do controle so leva os botoes e eixos do console)
    CameraRecenter = 1u << 12,
};
constexpr int kButtonCount = 13;

// CameraX/CameraY: o analogico da camera livre (N7c), separado do segundo analogico do console (Stick2), que o jogo le
enum class Axis : uint8_t { StickX, StickY, Stick2X, Stick2Y, TriggerL, TriggerR, CameraX, CameraY };
constexpr int kAxisCount = 8;

struct ControllerState {
    uint32_t buttons          = 0;
    float    axes[kAxisCount] = {};  // Axis: analogicos -1..1, gatilhos 0..1
    bool     connected        = true;

    float& axis(Axis a) { return axes[int(a)]; }
    float  axis(Axis a) const { return axes[int(a)]; }
    bool   operator==(const ControllerState&) const = default;
};

// Nomes das acoes no arquivo de configuracao e nos mapas: "A", "START", "UP"..., "LT", "RT",
// "ANALOG_UP"/"ANALOG_DOWN"/"ANALOG_LEFT"/"ANALOG_RIGHT" (analogico pelo teclado).
const char* button_name(Button b);

// O que um controle fisico (tecla, botao ou eixo do controle) aciona.
struct Target {
    enum class Kind : uint8_t { Button, AxisDirection, AxisFull } kind = Kind::Button;
    uint32_t button = 0;   // Button
    Axis     axis   = Axis::StickX;
    float    sign   = 1;   // AxisDirection: -1 ou +1 (gatilho: +1); AxisFull: +1 ou -1 (inverter)
    bool     operator==(const Target&) const = default;
};

struct Binding {
    std::string control;  // teclado: nome da tecla ("Z", "ENTER", "UP"...); controle: "a", "dpup", "leftx"...
    Target      target;
};

struct Options {
    std::string backend          = "sdl";  // "sdl" (SDL3 Gamepad) ou "xinput"
    float       deadzone         = 0.0f;   // zona morta radial do host, 0..0.9 (0 = fiel ao jogo)
    bool        auto_switch      = true;   // o ultimo controle com atividade assume o jogador 1
    std::string preferred        = "";     // parte do nome do controle preferido (vazio: o ultimo usado)
};

// Perfil de um modelo de controle (N3-3), pelo GUID do dispositivo (o do SDL3, 32 digitos hexadecimais,
// minusculos; "xinput" para os controles do XInput de reserva). As acoes listadas substituem as do
// [controle]; as demais herdam dele. Controle sem perfil: o [controle] (fallback).
struct Profile {
    std::string          guid;
    std::string          name;      // so para o log
    std::vector<Binding> gamepad;   // ligacoes ja resolvidas (herdadas + as do perfil)
    float                deadzone = -1;  // < 0: a de [opcoes]
};

// Remapeamento: ligacoes do teclado e do controle para acoes, mais opcoes.
class ActionMap {
public:
    Options              options;
    std::vector<Binding> keyboard;
    std::vector<Binding> gamepad;
    std::vector<Profile> profiles;

    // Perfil do GUID (sem diferenca de maiusculas), ou nullptr.
    const Profile* profile_for(std::string_view guid) const;

    // Mapa padrao: teclado (setas, ENTER = START, Z/X/A/S = A/B/X/Y, Q/W = gatilhos, I/J/K/L =
    // analogico) e controle por posicao (Xbox: A embaixo, B a direita, X a esquerda, Y em cima).
    static ActionMap defaults();
    // Arquivo TOML do usuario (secoes [opcoes], [teclado], [controle]); acoes ausentes ficam com o
    // padrao. Erro com a linha e o motivo.
    static Result<ActionMap> parse(std::string_view toml, std::string* error);
    // Texto do arquivo padrao, comentado (criado na primeira execucao).
    static std::string default_text();
    // Mapa de teclado no formato antigo "ACAO = TECLA" por linha (opcao --map).
    Result<void> set_keyboard_legacy(std::string_view text, std::string* error);
};

// Indices que fontes de teste usam para decidir o estado de cada amostra (roteiros).
struct SampleContext {
    uint64_t sample = 0;  // numero da amostra no ponto de amostragem do jogo
};
using SampleFn = std::function<ControllerState(const SampleContext&)>;

struct ConnectionEvent {
    enum class Kind : uint8_t { Connected, Disconnected } kind;
    int         device;
    std::string name;
    std::string guid;     // N3-3: para o usuario montar um perfil
    std::string profile;  // nome do perfil aplicado (vazio: o [controle]), na conexao e na desconexao
};

// Dispositivos e jogador 1. Seguro entre threads: os eventos chegam pela thread da janela e a
// amostra e tirada pela thread do jogo.
class InputSystem {
public:
    explicit InputSystem(ActionMap map = ActionMap::defaults());
    ~InputSystem();

    const ActionMap& map() const { return map_; }

    // Teclado da janela (nomes do mapa); perder o foco solta as teclas (os controles nao).
    void key(std::string_view name, bool down);
    void release_keyboard();

    // Controles (backend SDL3 Gamepad, ou outro com nomes iguais). `id` e do backend.
    int  gamepad_added(uint32_t id, std::string_view name, std::string_view guid = {});
    void gamepad_removed(uint32_t id);
    void gamepad_button(uint32_t id, std::string_view control, bool down);
    void gamepad_axis(uint32_t id, std::string_view control, float value);  // -1..1; gatilhos 0..1

    // XInput de reserva (options.backend == "xinput"): consultado em cada amostra. `reader` substitui
    // o XInputGetState (testes); vazio = o do Windows.
    struct XInputPad {
        uint16_t buttons = 0;  // wButtons (XINPUT_GAMEPAD_*)
        uint8_t  lt = 0, rt = 0;
        int16_t  lx = 0, ly = 0, rx = 0, ry = 0;  // y para cima, como no XInput
    };
    using XInputReader = std::function<bool(int slot, XInputPad& out)>;
    void enable_xinput(XInputReader reader = {});

    // Fonte de teste exclusiva (roteiros): com ela, os dispositivos do host nao entram na amostra.
    void set_test_source(SampleFn fn);
    // Filtro da amostra dos dispositivos do host (gravacao de sessoes, so desenvolvimento): recebe o
    // estado ja combinado e devolve o que o jogo ve. Nao vale com a fonte de teste.
    using FilterFn = std::function<ControllerState(const SampleContext&, const ControllerState&)>;
    void set_filter(FilterFn fn);
    // Transformacao da amostra do jogo (N7c: movimentacao relativa a camera livre), depois de combinar os dispositivos do
    // host e ANTES do filtro: a gravacao guarda o efetivo e a reproducao (fonte de teste) o entrega sem transformar de
    // novo. `test_source` = vale tambem para a fonte de teste (so os diagnosticos da camera, BS_CAMERA_FIXA/ROTEIRO).
    void set_transform(FilterFn fn, bool test_source = false);

    // Amostra do jogador 1 no ponto de amostragem; fica guardada como a ultima amostra.
    ControllerState latch(const SampleContext& ctx);
    ControllerState last() const;
    // O estado dos dispositivos do host agora (teclado e controle ativo), sem congelar nada, sem filtro (gravacao) e mesmo
    // com a fonte de teste: para quem nao e o jogo (a camera livre, N7c).
    ControllerState peek();
    // Presenca do jogador 1 sem tirar amostra (sondagens do protocolo que nao leem o estado).
    // Com dispositivos do host, sempre presente (o teclado); com fonte de teste, o que ela diz.
    bool connected(const SampleContext& ctx);

    std::vector<ConnectionEvent> drain_events();
    int                          active_gamepad() const;  // indice do controle que alimenta o jogador 1, -1 = nenhum
    std::vector<std::string>     gamepad_names() const;

    struct Impl;

private:
    ActionMap             map_;
    std::unique_ptr<Impl> impl_;
};

// Conversoes de valores de backends (inteiros com sinal de 16 bits, sem sinal de 8 bits) e zona
// morta radial. Exatas e documentadas: as mesmas em todos os backends.
float axis_from_s16(int v);    // -32768..32767 -> -1..1 (com saturacao)
float trigger_from_u8(int v);  // 0..255 -> 0..1
float trigger_from_s16(int v); // 0..32767 -> 0..1
void  apply_radial_deadzone(float& x, float& y, float deadzone);

} // namespace bs::native::input
