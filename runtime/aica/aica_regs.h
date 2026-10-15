#pragma once
// Registradores do AICA que o boot usa (area 0): o relogio de tempo real e o
// registrador de controle em 0x00702C00. Audio e o ARM7 ficam para o M6.
#include "cpu/sh4_context.h"

namespace bs {

void aica_regs_reset();

// RAM de som (2 MB): 0x00800000-0x009FFFFF, repetida ate 0x00FFFFFF.
constexpr uint32_t kAicaRamSize = 2u << 20;
uint8_t*           aica_ram();

// RTC em segundos desde 1950-01-01 (epoca do Dreamcast, hora local, sem fuso).
// `cycles` e o instante (contador da CPU) em que o valor vale; o relogio avanca
// um por segundo emulado (200 000 000 ciclos).
void     aica_rtc_set(uint32_t seconds, uint64_t now);  // now: instante do provedor de tempo
uint32_t aica_rtc_host_now();  // hora atual do host na epoca do Dreamcast

// true se `a` e um registrador tratado aqui.
bool aica_reg_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool aica_reg_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

} // namespace bs
