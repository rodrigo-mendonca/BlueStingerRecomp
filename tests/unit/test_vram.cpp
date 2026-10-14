// VRAM do PowerVR (runtime/pvr/vram.cpp) pelo caminho de memoria do guest.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "pvr/vram.h"

#include <vector>

using namespace bs;

namespace {

struct VramFixture {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    VramFixture() {
        c.ram = ram.data();
        vram_reset();
    }
};

} // namespace

// Boot do Blue Stinger: zera 0xA5000000-0xA57FFFFF pelo caminho de 32 bits.
BS_TEST(vram_32bit_path_is_linear_with_mirrors) {
    VramFixture f;
    sh4_write32(&f.c, 0xA5000000, 0x11223344);
    sh4_write32(&f.c, 0xA57FFFFC, 0x55667788);
    BS_CHECK(vram_data()[0] == 0x44 && vram_data()[kVramSize - 1] == 0x55);
    BS_CHECK(sh4_read32(&f.c, 0x05000000) == 0x11223344);  // P0
    BS_CHECK(sh4_read32(&f.c, 0x85800000) == 0x11223344);  // espelho de 8 MB dentro da janela
    BS_CHECK(sh4_read32(&f.c, 0xA7000000) == 0x11223344);  // espelho 0x07
    BS_CHECK(sh4_read16(&f.c, 0xA5000002) == 0x1122);
    BS_CHECK(sh4_read8(&f.c, 0xA5000001) == 0x33);
}

// Caminho de 64 bits: palavras de 32 bits alternam entre os bancos (0-4 MB e 4-8 MB).
BS_TEST(vram_64bit_path_interleaves_banks) {
    VramFixture f;
    BS_CHECK(vram_offset64(0) == 0 && vram_offset64(4) == kVramSize / 2);
    BS_CHECK(vram_offset64(8) == 4 && vram_offset64(0xC) == kVramSize / 2 + 4);
    BS_CHECK(vram_offset64(0x7FFFFC) == kVramSize - 4);
    sh4_write32(&f.c, 0xA4000000, 0xAAAAAAAA);
    sh4_write32(&f.c, 0xA4000004, 0xBBBBBBBB);
    sh4_write32(&f.c, 0xA4000008, 0xCCCCCCCC);
    BS_CHECK(sh4_read32(&f.c, 0xA5000000) == 0xAAAAAAAA);
    BS_CHECK(sh4_read32(&f.c, 0xA5400000) == 0xBBBBBBBB);
    BS_CHECK(sh4_read32(&f.c, 0xA5000004) == 0xCCCCCCCC);
    BS_CHECK(sh4_read32(&f.c, 0xA6000004) == 0xBBBBBBBB);  // espelho 0x06
    sh4_write16(&f.c, 0xA4000006, 0x1234);
    BS_CHECK(sh4_read16(&f.c, 0xA5400002) == 0x1234);
}
