// CCN do SH-4 (manual SH7750, cap. 4 "Caches"):
//   CCR 0xFF00001C: valor inicial 0; ICI (bit 11) e OCI (bit 3) invalidam o cache
//                   e sempre leem 0.
//   0xF0000000-0xF7FFFFFF: arrays de enderecos/dados do IC e do OC. O boot do jogo
//                   zera o array de enderecos do OC (0xF4xxxxxx) para invalida-lo,
//                   rodando em P2; sem cache no host, escrever e ler nao tem efeito.
#include "memory/sh4_ccn.h"

namespace bs {
namespace {

constexpr uint32_t kCcr         = 0xFF00001C;
constexpr uint32_t kCcrWriteOnly = (1u << 11) | (1u << 3);  // ICI, OCI

uint32_t g_ccr = 0;

bool isCacheArray(uint32_t a) { return a >= 0xF0000000 && a < 0xF8000000; }

} // namespace

void sh4_ccn_reset() { g_ccr = 0; }

bool sh4_ccn_read(uint32_t a, unsigned size, uint32_t& v) {
    if (a == kCcr && size == 4) {
        v = g_ccr;
        return true;
    }
    if (isCacheArray(a)) {
        v = 0;  // entrada invalida (V = 0)
        return true;
    }
    return false;
}

bool sh4_ccn_write(uint32_t a, unsigned size, uint32_t v) {
    if (a == kCcr && size == 4) {
        g_ccr = v & ~kCcrWriteOnly;
        return true;
    }
    return isCacheArray(a);
}

} // namespace bs
