// Syscall FLASHROM (documentacao do KallistiOS, dc/flashrom.h; reios como referencia):
//   0 INFO(r4 = particao, r5 = [inicio, tamanho])       -> 0 ou -1 se nao existe
//   1 READ(r4 = posicao, r5 = destino, r6 = bytes)       -> 0
//   2 WRITE(r4 = posicao, r5 = origem, r6 = bytes)       -> bytes (flash so apaga bits: AND)
//   3 DELETE(r4 = inicio de uma particao)                -> 0 (particao inteira = 0xFF) ou -1
// Particoes: 0 fabrica 0x1A000/8 KB, 1 reservada 0x18000/8 KB, 2 usuario 0x1C000/16 KB,
// 3 jogo 0x10000/32 KB, 4 desconhecida 0x00000/64 KB.
#include "hle/flashrom.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"

#include <algorithm>
#include <fstream>
#include <vector>

namespace bs {
namespace {

struct Partition {
    uint32_t offset, size;
};
constexpr Partition kParts[] = {{0x1A000, 8u << 10}, {0x18000, 8u << 10}, {0x1C000, 16u << 10},
                                {0x10000, 32u << 10}, {0x00000, 64u << 10}};

std::vector<uint8_t> g_flash(kFlashSize, 0xFF);

bool inFlash(uint32_t off, uint32_t n) { return off <= kFlashSize && n <= kFlashSize - off; }

} // namespace

bool flashrom_load(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    if (d.size() != kFlashSize) return false;
    g_flash = std::move(d);
    return true;
}

uint8_t* flashrom_data() { return g_flash.data(); }

void flashrom_reset() { std::fill(g_flash.begin(), g_flash.end(), uint8_t(0xFF)); }

void flashrom_syscall(Sh4Context* c) {
    uint32_t&      r0 = c->r[0];
    const uint32_t r4 = c->r[4], r5 = c->r[5], r6 = c->r[6];
    switch (c->r[7]) {
    case 0:  // INFO
        if (r4 >= std::size(kParts)) {
            r0 = 0xFFFFFFFF;
            return;
        }
        sh4_write32(c, r5, kParts[r4].offset);
        sh4_write32(c, r5 + 4, kParts[r4].size);
        r0 = 0;
        return;
    case 1:  // READ
        if (!inFlash(r4, r6)) {
            r0 = 0xFFFFFFFF;
            return;
        }
        for (uint32_t i = 0; i < r6; ++i) sh4_write8(c, r5 + i, g_flash[r4 + i]);
        r0 = 0;
        return;
    case 2:  // WRITE
        if (!inFlash(r4, r6)) {
            r0 = 0xFFFFFFFF;
            return;
        }
        for (uint32_t i = 0; i < r6; ++i) g_flash[r4 + i] &= sh4_read8(c, r5 + i);
        r0 = r6;
        return;
    case 3:  // DELETE
        for (const auto& p : kParts)
            if (p.offset == r4) {
                std::fill(g_flash.begin() + p.offset, g_flash.begin() + p.offset + p.size, uint8_t(0xFF));
                r0 = 0;
                return;
            }
        r0 = 0xFFFFFFFF;
        return;
    default:
        sh4_fatal(c, "funcao FLASHROM desconhecida", c->pc, c->r[7]);
    }
}

} // namespace bs
