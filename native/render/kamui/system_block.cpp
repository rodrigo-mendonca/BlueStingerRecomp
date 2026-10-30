// N8.7 (docs/N8-7-KAMUI.md, passo 5b): o system block do Holly que sobra no produto (system_block.h).
#include "render/kamui/system_block.h"

namespace bs::native::render::kamui {

SystemBlock::Region SystemBlock::removed(uint32_t p, unsigned size, bool write, uint32_t v) {
    const bool gd = p >= kGdBase && p < kGdEnd, g1 = p >= kG1Base && p < kG1End;
    if (!gd && !g1) return Region::None;
    if (g1 && write && size == 4 && p == kG1Gdst && v == 0) return Region::None;  // DMA parado (tabela do SDK)
    return gd ? Region::GdRom : Region::G1;
}

void SystemBlock::reset() {
    regs_.fill(0);
    reported_.fill(false);
}

SystemBlock::Read SystemBlock::read(uint32_t p) {
    if (p == kSbRev) return {kSbRevValue, false};
    const uint32_t i = (p - kBase) / 4;
    Read           r{regs_[i], !reported_[i]};
    reported_[i] = true;
    return r;
}

SystemBlock::Action SystemBlock::write(uint32_t p, uint32_t v) {
    if (p != kSbRev) regs_[(p - kBase) / 4] = v;
    if (p == kC2Dst && (v & 1)) return Action::Ch2Start;
    if ((p == 0x005F7818 || p == 0x005F7838 || p == 0x005F7858 || p == 0x005F7878) && (v & 1)) return Action::G2Start;
    return Action::None;
}

} // namespace bs::native::render::kamui
