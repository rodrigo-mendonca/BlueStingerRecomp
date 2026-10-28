#pragma once
// Registradores do AICA que o boot usa (area 0): o relogio de tempo real e o
// registrador de controle em 0x00702C00. Audio e o ARM7 ficam para o M6.
// N8.6: modelo de validacao e das ferramentas (bs_runtime_hle); o produto usa a area de som nativa (system/sound_bus),
// que reproduz este modelo no contrato do jogo. Os consumidores usam a fachada snd_bus_*.
#include "cpu/sh4_context.h"
#include "system/sound_bus.h"

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
// Este modelo como provedor da fachada snd_bus_* (validacao: audio legado e os diagnosticos BS_AICA_*).
const SoundBusBackend* aica_bus_backend();

} // namespace bs
