#include "test.h"

#include "cpu/sh4_context.h"

BS_TEST(reset_sets_entry_and_sr) {
    bs::Sh4Context c;
    bs::sh4_reset(c, 0x8C010000);
    BS_CHECK(c.pc == 0x8C010000);
    BS_CHECK(c.sr == 0x700000F0);
    BS_CHECK(c.t == 0);
}
