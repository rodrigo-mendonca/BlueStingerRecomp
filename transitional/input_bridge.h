#pragma once
// Ponte da entrada na fase hibrida (N3-1). O nucleo nativo (native/platform/input.h) produz o estado
// logico do jogador 1; o legado (Maple, runtime/maple) le pela fonte de entrada do runtime
// (input_source). Esta ponte instala uma fonte que tira a amostra do InputSystem no momento da
// leitura do Maple (o ponto de amostragem do jogo, docs/N3-PLANO.md 3.1) e so traduz o estado.
// Some com o Maple de controle (N3-2 em diante, provedor nativo do pd).
//
// Conversoes (docs/N3-PLANO.md 3.1, decisao 3), exatas nos 256 valores de ida e volta:
//   eixo [-1, 1] -> byte = clamp(round_half_away(127,5 v + 127,5), 0, 255); byte -> (b - 127,5) / 127,5
//   gatilho [0, 1] -> byte = clamp(round_half_away(255 v), 0, 255);        byte -> b / 255
#include "input/input.h"
#include "platform/input.h"

#include <cstdint>
#include <memory>

namespace bs::transitional::input_bridge {

uint8_t axis_byte(float v);
float   axis_from_byte(uint8_t b);
uint8_t trigger_byte(float v);
float   trigger_from_byte(uint8_t b);

native::input::ControllerState to_controller(const PadState& p);
PadState                       to_pad(const native::input::ControllerState& s);

// Fonte do runtime que tira a amostra de `sys` a cada leitura do Maple (porta A; as demais vazias
// como sempre). nullptr desinstala. O dono de `sys` e o chamador.
void install(native::input::InputSystem* sys);

// Fonte de teste do InputSystem a partir de uma fonte do runtime (roteiros, gameplay assistido,
// teclado e XInput do modo BS_INPUT=host): o estado vem da fonte e passa pelas conversoes.
native::input::SampleFn from_runtime_source(std::shared_ptr<InputSource> src);

} // namespace bs::transitional::input_bridge
