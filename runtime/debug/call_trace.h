#pragma once
// Diagnostico (N1, identificacao por contrato): BS_CALL_TRACE=<arquivo.tsv>;<end>,<end>,...
// envolve as funcoes pedidas pelo mecanismo dos mods e grava, por chamada: funcao, quem
// chamou (cur_fn), r4-r7, r0 no retorno, PR (ponto da chamada; N2b) e o VBlank, e as
// strings ASCII apontadas por r4-r7 (se houver).
// Com slots (build BS_MODS) pega chamadas diretas e indiretas; sem slots, so as indiretas.
// Desligado por padrao; nao muda o comportamento (o original e chamado do mesmo jeito).
#include "cpu/sh4_context.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bs {

// Instala os envoltorios (a partir de BS_CALL_TRACE). Chamado pelo GameHost depois dos mods.
void call_trace_install();
// Para testes: instala com caminho e enderecos explicitos (vazio desinstala).
void call_trace_install(const std::string& path, const std::vector<uint32_t>& addrs);
bool call_trace_flush();

} // namespace bs
