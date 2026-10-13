#include "test.h"

#include "cpu/sh4_context.h"
#include "decoder/sh4_decoder.h"

BS_TEST(reset_sets_entry_and_sr) {
    bs::Sh4Context c;
    bs::sh4_reset(c, 0x8C010000);
    BS_CHECK(c.pc == 0x8C010000);
    BS_CHECK(c.sr == 0x700000F0);
    BS_CHECK(c.t == 0);
}

BS_TEST(decoder_stub_returns_invalid) {
    BS_CHECK(bs::sh4::decode(0x0009).op == bs::sh4::Op::Invalid);  // vira NOP no M2
}
