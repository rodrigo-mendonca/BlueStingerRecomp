#pragma once
// Controle de cache do SH-4 (CCN): CCR e os arrays de enderecos/dados dos caches.
// O codigo nativo nao tem cache a emular; o que importa e o valor lido de volta.
#include <cstdint>

namespace bs {

void sh4_ccn_reset();

// true se `a` pertence ao CCN (o acesso foi tratado).
bool sh4_ccn_read(uint32_t a, unsigned size, uint32_t& v);
bool sh4_ccn_write(uint32_t a, unsigned size, uint32_t v);

} // namespace bs
