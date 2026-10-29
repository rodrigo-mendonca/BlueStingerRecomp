#include "render/kamui/event_status.h"

namespace bs::native::render::kamui {

void EventStatus::reset() {
    regs_.fill(0);
    vblanks_ = fused_ = 0;
    // events_ nao zera: e uma epoca do processo (diagnostico do N2b), como no controlador anterior
}

void EventStatus::deliver(const Due& d) {
    if (d.any()) ++events_;
    if (d.vblank_in) {
        if (at(kIstNrm) & kVblankIn) ++fused_;
        at(kIstNrm) |= kVblankIn;
        ++vblanks_;
    }
    if (d.vblank_out) at(kIstNrm) |= kVblankOut;
    if (d.render) at(kIstNrm) |= kRenderDone;
    if (d.maple) at(kIstNrm) |= kMapleDma;
}

void EventStatus::raise(uint32_t bits) {
    at(kIstNrm) |= bits;
    ++events_;
}

void EventStatus::write(uint32_t p, uint32_t v) {
    if (p == kIstNrm || p == kIstExt || p == kIstErr) at(p) &= ~v;  // escrever 1 limpa
    else at(p) = v;
}

uint32_t EventStatus::irl_level() const {
    const uint32_t nrm = reg(kIstNrm), ext = reg(kIstExt), err = reg(kIstErr);
    // IML6NRM 0x005F6930, IML4NRM 0x005F6920, IML2NRM 0x005F6910; EXT +4, ERR +8
    for (uint32_t level : {6u, 4u, 2u}) {
        const uint32_t m = 0x005F6910 + (level / 2 - 1) * 0x10;
        if ((nrm & reg(m)) || (ext & reg(m + 4)) || (err & reg(m + 8))) return level;
    }
    return 0;
}

} // namespace bs::native::render::kamui
