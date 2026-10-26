#pragma once
// N8.1: mutacao BS_CICLOS_RUIDO=<n> (prova de independencia dos ciclos). Soma 0..n-1 ciclos estimados (sequencia fixa)
// a cada atendimento (sh4_service) e a cada checagem de vivacidade (sh4_progress_tick). No tempo logico (cadencia B)
// nenhum artefato pode mudar; no estavel (controle) o tempo muda. Desligada, custa um teste por ponto.
#include "cpu/sh4_context.h"

namespace bs {

void cycle_noise(Sh4Context* c);

} // namespace bs
