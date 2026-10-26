#pragma once
// Porta A de E/S do SH-4 (PCTRA/PDTRA). No Dreamcast os pinos 8-9 informam o
// tipo de cabo de video; o boot do jogo le PDTRA para escolher o modo de video.
#include <cstdint>

namespace bs {

enum class VideoCable : uint32_t { Vga = 0, Rgb = 2, Composite = 3 };

// Padrao: composto, o mesmo do Flycast usado como oraculo (testes diferenciais).
void       sh4_port_set_cable(VideoCable c);
VideoCable sh4_port_cable();
void       sh4_port_reset();
// N8.1: o servico do tempo logico (memory/sys_logical) entrega PCTRA e PDTRA ao modelo fora do contrato.
void       sh4_port_adopt(uint32_t pctra, uint16_t pdtra);

// true se `a` e um registrador da porta (o acesso foi tratado).
bool sh4_port_read(uint32_t a, unsigned size, uint32_t& v);
bool sh4_port_write(uint32_t a, unsigned size, uint32_t v);

} // namespace bs
