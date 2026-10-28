#pragma once
// N8.6 (docs/N8-6-AUDIO.md): a area de som do produto. Com os tres provedores de audio nativos (efeitos, falas e musica,
// N6), o som vem do mixer nativo; o SDK de som recompilado continua a inicializar e consultar a area da AICA (codigo do
// guest, AOT): limpa e preenche a RAM de som (8C08EE60), le uma palavra de estado (0x00810000) e o RTC a cada VBlank
// (8C08EBD0) e mexe no controle 0x00702C00. Este servico atende so isso, sem AICA, ARM7 nem driver:
//  - RAM de som: 2 MB de memoria do guest (0x00800000-0x009FFFFF, repetida ate 0x00FFFFFF), so guardada e devolvida;
//  - controle 0x00702C00: o valor escrito volta na leitura (nenhum ARM7 sai do reset);
//  - RTC (0x00710000 altos, 0x00710004 baixos, 0x00710008 liberacao da escrita): a hora do sistema no relogio do
//    provedor de tempo, como o modelo;
//  - a mesma instrumentacao do modelo (BS_CLOCK_LOG: AudRd, Aud, Rtc; BS_INTC_TRACE: Rtc), que as referencias comparam.
// Qualquer outro registrador da AICA (0x00700000-0x0071FFFF: canais, DSP, temporizadores, interrupcoes do ARM7), ou
// acesso de outro tamanho, e um componente removido: erro explicito (sh4_fatal), contado. O modelo (runtime/aica, com os
// diagnosticos BS_AICA_TRACE/LOG/MUTACAO) fica na validacao (provedores de audio legados) e nas ferramentas.
#include "cpu/sh4_context.h"

#include <cstdint>

namespace bs {

// Fachada da area de som (memoria, boot, diagnostico do CRI): o provedor ativo. Padrao: o servico nativo; a validacao
// pode instalar o modelo (aica_bus_backend, runtime/aica).
struct SoundBusBackend {
    void (*reset)();
    void (*rtc_set)(uint32_t seconds, uint64_t now);
    bool (*read)(Sh4Context*, uint32_t, unsigned, uint32_t&);
    bool (*write)(Sh4Context*, uint32_t, unsigned, uint32_t);
    uint8_t* (*ram)();
};
void     snd_bus_use(const SoundBusBackend* b);  // nullptr = servico nativo
void     snd_bus_reset();
void     snd_bus_rtc_set(uint32_t seconds, uint64_t now);  // RTC em segundos desde 1950 (now: instante do provedor)
bool     snd_bus_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool     snd_bus_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);
uint8_t* snd_bus_ram();  // 2 MB (diagnostico)

// Hora atual do host na epoca do Dreamcast (segundos desde 1950-01-01, hora local).
uint32_t snd_rtc_host_now();
// Pedidos fora do contrato no servico nativo (erro explicito; meta 0).
uint64_t sound_bus_refused();

} // namespace bs
