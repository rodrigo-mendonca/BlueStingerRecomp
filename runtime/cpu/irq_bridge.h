#pragma once
// N8.1: ponte de interrupcoes no tempo logico. No --tempo experimental (cadencia B) a entrega nao passa pelo modelo do
// INTC (cpu/sh4_intc): a ponte leva o nivel IRL do Holly (holly_irl_level, fontes que vem das filas de eventos nativas)
// ao tratador do jogo pelo contrato observavel de 8C00FA00 (VBR + 0x600):
//  - aceitacao: SR.BL = 0 e nivel > SR.IMASK; o maior nivel pendente; INTEVT = 0x200 + (15 - nivel) * 0x20
//    (0x320/0x360/0x3A0 para os niveis 6/4/2);
//  - entrada: SSR = SR, SPC = c->pc (o ponto de retomada confiavel: poll, BS_IRQ_CHECK, espera do hardware),
//    SGR = R15, SR |= MD | RB | BL; o tratador recompilado roda aninhado e o rte confere o SPC (ret_expect);
//  - depois do rte, a reavaliacao ate nao haver nivel entregavel (uma fonte ainda acesa e aceita antes de retomar);
//  - os registros E e I da instrumentacao, iguais aos do modelo.
// Registradores no contrato do boot: INTEVT (0xFF000028, 32 bits) lido pelo tratador; ICR e IPRA-IPRC (16 bits) escritos
// uma vez por 8C08BD74 e nunca lidos (o nivel IRL nao passa pelas IPR). Fora do contrato (leitura de ICR/IPR, escrita do
// INTEVT, outro tamanho) o estado vai para o modelo (sh4_intc_adopt), que atende tudo daqui em diante, inclusive a
// entrega; o acesso e contado (irq_bridge_fallbacks: 0 nas rotas conferidas). No tempo estavel nada aqui roda.
#include "cpu/sh4_context.h"

#include <cstdint>

namespace bs {

bool     irq_bridge_active();            // tempo logico, sem ter entregue o estado ao modelo
void     irq_bridge_check(Sh4Context* c);  // sh4_irq_check no tempo logico
bool     irq_bridge_read(uint32_t a, unsigned size, uint32_t& v);
bool     irq_bridge_write(uint32_t a, unsigned size, uint32_t v);
void     irq_bridge_reset();
uint64_t irq_bridge_taken();      // entregas pela ponte (somadas em sh4_irq_taken_count)
uint64_t irq_bridge_fallbacks();  // acessos fora do contrato (diagnostico; meta 0)
uint64_t irq_bridge_served();     // acessos a registradores atendidos (diagnostico)

} // namespace bs
