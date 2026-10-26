#pragma once
// Controle de cache do SH-4 (CCN): CCR e os arrays de enderecos/dados dos caches.
// O codigo nativo nao tem cache a emular; o que importa e o valor lido de volta.
#include <cstdint>

namespace bs {

void sh4_ccn_reset();
// N8.1: o servico do tempo logico (memory/sys_logical) entrega o CCR ao modelo quando o jogo sai do contrato.
void sh4_ccn_adopt(uint32_t ccr);

// true se `a` pertence ao CCN (o acesso foi tratado).
bool sh4_ccn_read(uint32_t a, unsigned size, uint32_t& v);
bool sh4_ccn_write(uint32_t a, unsigned size, uint32_t v);

} // namespace bs
