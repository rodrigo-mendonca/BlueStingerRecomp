#pragma once
// Programa SH-4 sintetico (escrito a mao, nao vem do jogo) para testar analise e
// geracao de codigo sem dados do usuario. Base 0x8C010000.
//
//   main(r4 = n, r5 = caso):
//     r0 = soma 1..n          (laco com dt + bf/s)
//     bsr double; slot: mov r0,r8   -> r8 recebe a soma ANTES de double (r0 *= 2)
//     switch (r5) via mova + mov.w + braf: +100, +200, +300 (via pool); >= 3: nada
//     jsr @r3 com r3 = constante do pool (set1234: r1 = 0x1234)
//     return r0 = r8
#include <cstdint>

namespace bs::fixture {

constexpr uint32_t kBase     = 0x8C010000;
constexpr uint32_t kMain     = 0x8C010000;
constexpr uint32_t kBraf     = 0x8C010022;
constexpr uint32_t kTable    = 0x8C01004C;
constexpr uint32_t kJsr      = 0x8C01003C;
constexpr uint32_t kDouble   = 0x8C01005C;
constexpr uint32_t kSet1234  = 0x8C010060;

inline constexpr uint16_t kWords[] = {
    0x4F22, 0x2F86, 0xE000, 0x304C, 0x4410, 0x8FFC, 0x0009, 0xB025,
    0x6803, 0xE103, 0x3512, 0x8910, 0x6053, 0x4000, 0x6103, 0xC70B,
    0x001D, 0x0023, 0x0009, 0x7864, 0xA007, 0x0009, 0x7864, 0x7864,
    0xA003, 0x0009, 0xD108, 0xA000, 0x381C, 0xD306, 0x430B, 0x0009,
    0x6083, 0x68F6, 0x4F26, 0x000B, 0x0009, 0x0009, 0x0000, 0x0006,
    0x000E, 0x0000, 0x0060, 0x8C01, 0x012C, 0x0000, 0x000B, 0x300C,
    0x9101, 0x000B, 0x0009, 0x1234,
};

inline uint32_t expected(uint32_t n, uint32_t sel) {
    const uint32_t sum = n * (n + 1) / 2;
    return sum + (sel == 0 ? 100 : sel == 1 ? 200 : sel == 2 ? 300 : 0);
}

} // namespace bs::fixture
