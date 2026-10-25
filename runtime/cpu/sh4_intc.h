#pragma once
// INTC do SH-4 e entrega de interrupcoes ao codigo recompilado (R1 do ARCHITECTURE.md).
// A interrupcao e entregue como chamada aninhada ao tratador em VBR+0x600, nos pontos
// de poll do codigo gerado: back-edges (BS_POLL) e logo depois de `ldc ...,sr`, onde
// uma interrupcao pendente pode ser desmascarada.
#include "cpu/sh4_context.h"
#include "debug/intc_trace.h"

namespace bs {

void sh4_intc_reset();

// Entrega a interrupcao pendente de maior prioridade, se SR permitir. c->pc deve ser
// o endereco de retomada (vira SPC).
void sh4_irq_check(Sh4Context* c);

// Registradores do INTC (ICR, IPRA-C) e INTEVT; true se o acesso foi tratado.
bool sh4_intc_read(uint32_t a, unsigned size, uint32_t& v);
bool sh4_intc_write(uint32_t a, unsigned size, uint32_t v);

// Interrupcoes aceitas desde o inicio do processo (diagnostico N2b: epoca de eventos).
uint64_t sh4_irq_taken_count();

// Diagnostico BS_INTC_TRACE (N8.1): de onde vem a entrega (registro E). sh4_service marca a cadencia ou o atendimento
// forcado (next_event = 0); quem chama sh4_service por outro motivo avisa antes (espera do hardware, sleep). Fora de
// sh4_service, a entrega e a do BS_IRQ_CHECK (ldc ...,sr). So observa: nada aqui muda a entrega.
void       sh4_irq_origin_hint(uint8_t origin);  // IntcSource (debug/intc_trace.h)
IntcSource sh4_irq_origin_enter(const Sh4Context* c);  // sh4_service: marca e devolve a anterior
void       sh4_irq_origin_leave(IntcSource prev);
uint8_t    sh4_irq_origin_now();  // origem do sh4_service em curso (IntcSource), 0 fora dele

} // namespace bs
