#pragma once
// N8.1: CCN nao grafico e porta do cabo no tempo logico, como o TMU (memory/tmu_logical.h): no --tempo experimental o
// acesso nao passa pelos modelos (memory/sh4_ccn, memory/sh4_port); e atendido aqui pelo contrato que o jogo usa, com o
// mesmo valor. Fora do contrato o estado vai para o modelo, que atende daqui em diante, e o acesso e contado
// (sys_logical_fallbacks: 0 nas rotas conferidas). No tempo estavel nada aqui roda.
//  - CCR (0xFF00001C, 32 bits): o boot e as rotinas de cache (8C010000, 8C158AB4, 8C17B6F8, 8C17B7CC, 8C17B9F0,
//    8C17BE18, 8C17C460, 8C191D0C) leem e escrevem; ICI e OCI (bits 11 e 3) invalidam e sempre leem 0;
//  - arrays de enderecos/dados do cache (0xF0000000-0xF7FFFFFF): o host nao tem cache. As escritas invalidam linhas
//    (8C158AA2, 8C17BE18) e nao tem efeito; a leitura (8C17B7CC, 8C17B9F0) le 0 (linha invalida): o valor so forma o
//    endereco do ocbi seguinte (8C17B830-8C17B842, sem efeito no host), e a saida do laco nao depende dele;
//  - PDTRA (0xFF800030, 16 bits, lido 2 vezes por 8C15E86E no boot): com o PCTRA em 0 (nunca escrito: todos os pinos
//    sao entrada) le o tipo de cabo nos pinos 8-9 (sh4_port_cable, composto por padrao). Escrita no PCTRA ou no PDTRA,
//    e os espelhos da area 7, saem do contrato.
#include <cstdint>

namespace bs {

bool sys_logical_read(uint32_t a, unsigned size, uint32_t& v);
bool sys_logical_write(uint32_t a, unsigned size, uint32_t v);
void sys_logical_reset();
uint64_t sys_logical_fallbacks();
uint64_t sys_logical_served();

} // namespace bs
