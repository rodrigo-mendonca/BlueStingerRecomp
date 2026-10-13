// Executa o C++ gerado para o programa sintetico e compara com o resultado
// esperado (semantica de fluxo: laco, delay slots, bsr, switch braf, jsr, rts).
#include "../fixtures/sh4_fixture.h"
#include "funcs.h"

#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    namespace fx = bs::fixture;
    bsg::register_functions();
    std::vector<uint8_t> ram(16u << 20);
    std::memcpy(ram.data() + (fx::kBase & 0xFFFFFF), fx::kWords, sizeof(fx::kWords));

    int fails = 0;
    for (uint32_t n : {1u, 10u, 100u}) {
        for (uint32_t sel : {0u, 1u, 2u, 3u, 77u}) {
            bs::Sh4Context c{};
            c.ram = ram.data();
            bs::sh4_reset(c, fx::kMain);
            bs::sh4_set_sr(c, 0x40000000);
            const uint32_t sentinel = 0x8C0FFFF0;
            c.r[4] = n; c.r[5] = sel; c.r[8] = 0xAAAA5555; c.r[15] = 0x8C100000;
            c.pr = sentinel; c.ret_expect = sentinel;
            bsg::f_8C010000(&c);
            const bool ok = c.r[0] == fx::expected(n, sel) && c.r[1] == 0x1234 && c.r[8] == 0xAAAA5555 &&
                            c.r[15] == 0x8C100000 && c.pr == sentinel;
            if (!ok) {
                ++fails;
                std::printf("FALHA n=%u sel=%u: r0=%u (esperado %u) r1=%X r8=%X r15=%X pr=%X\n", n, sel, c.r[0],
                            fx::expected(n, sel), c.r[1], c.r[8], c.r[15], c.pr);
            }
        }
    }
    std::printf("[%s] codigo gerado do programa sintetico\n", fails ? "FAIL" : " ok ");
    return fails ? 1 : 0;
}
