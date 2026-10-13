#include "cpu/sh4_context.h"

#include <cstring>

namespace bs {

void sh4_reset(Sh4Context& c, uint32_t entry) {
    std::memset(&c, 0, sizeof(c));
    c.sr    = 0x700000F0;  // MD=1 RB=1 BL=1 IMASK=0xF (estado de reset)
    c.fpscr = 0x00040001;  // valor de reset do SH-4
    c.pc    = entry;
    // M4: estado pos-BIOS (pilha, VBR, SR) definido pelo HLE de boot.
}

} // namespace bs
