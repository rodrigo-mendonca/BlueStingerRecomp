#pragma once
// N8.1 (aprovado pelo mantenedor em 2026-10-25): servico nativo do temporizador no tempo logico. No --tempo
// experimental (cadencia B, time_provider().cycle_cadence() == false) o acesso ao TMU nao passa pelo modelo de
// registradores (memory/sh4_tmu): e atendido aqui, sobre o instante logico, pelo contrato que o jogo usa:
//  - 8C15D17C (inicializacao, 2 vezes no boot): TOCR = 0, TSTR &= ~1, TCR0 = 2 (P/64, sem UNIE), TCOR0 = TCNT0 =
//    0xFFFFFFFF, TSTR |= 1: o canal 0 livre, decrescente, a 200 MHz / 4 / 64 = 1 tick a cada 256 ticks logicos;
//  - leituras do TCNT0: 8C15D1A2 (devolve 0xFFFFFFFF - TCNT0) e os 4 medidores do CRI (8C18BCAA, 8C1C4DE6, 8C1C51D2,
//    8C1C5438), inalterados: o codigo recompilado continua lendo o endereco.
// O valor e o mesmo do modelo para esse contrato: TCNT0 = 0xFFFFFFFF - (instante - partida) / 256, modulo 2^32 (com
// TCOR0 = 0xFFFFFFFF o periodo e 2^32; UNF acende na primeira volta).
// Qualquer acesso fora do contrato (outro canal, outro divisor, leitura de TCR0/TCOR0/TOCR, escrita de outro valor) entrega
// o estado ao modelo (sh4_tmu_adopt) e daqui em diante o modelo atende tudo; cada acesso assim e contado
// (tmu_logical_fallbacks: 0 em todas as rotas conferidas). No tempo estavel nada aqui roda.
// Diagnosticos: BS_TMU_DIAG (N2b) vale so no modelo: com ele ligado o servico entrega tudo ao modelo. Mutacao
// BS_TMU_MUTACAO=parado: o TCNT0 fica parado na partida (negativo: o timeout pelo TMU nao vence).
#include "cpu/sh4_context.h"

#include <cstdint>

namespace bs {

// true se o servico atendeu o acesso (tempo logico, dentro do contrato).
bool tmu_logical_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);
bool tmu_logical_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);
void tmu_logical_reset();
// estado mutavel do servico (detector de ociosidade da thread 3, junto com sh4_tmu_state_hash)
uint64_t tmu_logical_state_hash();
// acessos entregues ao modelo por estarem fora do contrato (diagnostico; meta 0)
uint64_t tmu_logical_fallbacks();
// acessos atendidos pelo servico (diagnostico)
uint64_t tmu_logical_served();

} // namespace bs
