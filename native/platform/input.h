#pragma once
// Controles (N3). Substitui o barramento Maple e a serializacao do pad do Dreamcast: o jogo
// recebe o estado logico de cada porta, montado a partir de teclado, mouse e controles do
// host (XInput etc.).
#include <cstdint>

namespace bs::native::input {

// Botoes logicos do controle do jogo (nao sao os bits do protocolo Maple).
enum Button : uint32_t {
    A          = 1u << 0,
    B          = 1u << 1,
    X          = 1u << 2,
    Y          = 1u << 3,
    Start      = 1u << 4,
    DpadUp     = 1u << 5,
    DpadDown   = 1u << 6,
    DpadLeft   = 1u << 7,
    DpadRight  = 1u << 8,
};

struct PadState {
    uint32_t buttons   = 0;     // Button
    float    stick_x   = 0.0f;  // -1..1
    float    stick_y   = 0.0f;  // -1..1 (positivo = baixo, como no jogo)
    float    trigger_l = 0.0f;  // 0..1
    float    trigger_r = 0.0f;  // 0..1
};

enum class Device : uint8_t { None, Pad, Vibration, Memory };

class IInput {
public:
    virtual ~IInput() = default;
    virtual int      port_count() const = 0;
    virtual bool     connected(int port) const = 0;
    virtual Device   device(int port, int slot) const = 0;  // slot 0 = controle; 1-2 = expansoes
    virtual PadState read(int port) = 0;                    // amostra do quadro corrente
    virtual void     vibrate(int port, float intensity, uint32_t duration_ms) = 0;
};

} // namespace bs::native::input
