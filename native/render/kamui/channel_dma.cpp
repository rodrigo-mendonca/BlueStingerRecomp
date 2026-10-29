#include "render/kamui/channel_dma.h"

namespace bs::native::render::kamui {

void ChannelDma::reset() {
    for (auto& ch : ch_)
        for (auto& r : ch) r = 0;
    dmaor_ = 0;
}

uint32_t ChannelDma::reg(uint32_t a) const {
    if (a == kDmaor) return dmaor_;
    return ch_[(a - kBase) >> 4][((a - kBase) & 0xF) >> 2];
}

void ChannelDma::write(uint32_t a, uint32_t v) {
    if (a == kDmaor) dmaor_ = v;
    else ch_[(a - kBase) >> 4][((a - kBase) & 0xF) >> 2] = v;
}

ChannelDma::Result ChannelDma::ch2_start(SbRegs& sb, Reader read, Sink sink, void* ctx) {
    uint32_t* ch = ch_[2];
    if (!(dmaor_ & kDme) || !(ch[3] & kDe)) return Result::Disabled;
    const uint32_t dest = sb.c2dstat, len = sb.c2dlen;
    const bool     fifo = (dest & 0x01800000) == 0;
    uint32_t       src  = ch[0];
    for (uint32_t off = 0; off < len; off += 32, src += 32) {
        uint32_t blk[8];
        for (int i = 0; i < 8; ++i) blk[i] = read(ctx, src + 4 * i);
        if (!sink(ctx, fifo ? dest : dest + off, blk)) return Result::BadDest;
    }
    ch[0] = src;
    ch[2] = 0;
    ch[3] |= kTe;
    sb.c2dstat = dest + len;
    sb.c2dlen  = 0;
    sb.c2dst   = 0;
    ++transfers_;
    return Result::Done;
}

} // namespace bs::native::render::kamui
