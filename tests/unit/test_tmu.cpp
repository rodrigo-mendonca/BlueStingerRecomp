// TMU do SH-4 (runtime/memory/sh4_tmu.cpp) pelo caminho de memoria do guest.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "memory/sh4_tmu.h"

#include <vector>

using namespace bs;

namespace {

struct TmuFixture {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    TmuFixture() {
        c.ram = ram.data();
        sh4_tmu_reset();
    }
};

} // namespace

// Uso do jogo: canal 0 livre, P/64 (256 ciclos da CPU por tick), contando de 0xFFFFFFFF.
BS_TEST(tmu_channel0_free_running_counter) {
    TmuFixture f;
    sh4_write8(&f.c, 0xFFD80004, 0);             // TSTR: parado
    sh4_write32(&f.c, 0xFFD80008, 0xFFFFFFFF);   // TCOR0
    sh4_write32(&f.c, 0xFFD8000C, 0xFFFFFFFF);   // TCNT0
    sh4_write16(&f.c, 0xFFD80010, 2);            // TCR0: P/64
    f.c.cycles += 5000;
    BS_CHECK(sh4_read32(&f.c, 0xFFD8000C) == 0xFFFFFFFF);  // parado
    sh4_write8(&f.c, 0xFFD80004, 1);             // inicia o canal 0
    f.c.cycles += 256 * 10 + 255;
    BS_CHECK(sh4_read32(&f.c, 0xFFD8000C) == 0xFFFFFFFF - 10);
    sh4_write8(&f.c, 0xFFD80004, 0);             // para: o valor congela
    f.c.cycles += 100000;
    BS_CHECK(sh4_read32(&f.c, 0xFFD8000C) == 0xFFFFFFFF - 10);
    BS_CHECK(sh4_read8(&f.c, 0xFFD80004) == 0);
}

// Underflow: recarrega TCOR e acende UNF; UNF so e limpo escrevendo 0.
BS_TEST(tmu_underflow_reloads_and_sets_unf) {
    TmuFixture f;
    sh4_write32(&f.c, 0xFFD80008, 9);   // TCOR0
    sh4_write32(&f.c, 0xFFD8000C, 1);   // TCNT0
    sh4_write16(&f.c, 0xFFD80010, 0);   // P/4: 16 ciclos por tick
    sh4_write8(&f.c, 0xFFD80004, 1);
    f.c.cycles += 16 * 3;                // 1 -> 0 -> 9 (underflow) -> 8
    BS_CHECK(sh4_read32(&f.c, 0xFFD8000C) == 8);
    BS_CHECK((sh4_read16(&f.c, 0xFFD80010) & 0x100) != 0);
    sh4_write16(&f.c, 0xFFD80010, 0x100);  // escrever 1 em UNF nao muda
    BS_CHECK((sh4_read16(&f.c, 0xFFD80010) & 0x100) != 0);
    sh4_write16(&f.c, 0xFFD80010, 0);      // escrever 0 limpa
    BS_CHECK((sh4_read16(&f.c, 0xFFD80010) & 0x100) == 0);
    BS_CHECK(sh4_read32(&f.c, 0xFFD8000C) == 8);
}
