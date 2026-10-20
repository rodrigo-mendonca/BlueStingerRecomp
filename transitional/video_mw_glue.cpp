#include "video_mw_glue.h"

namespace bs::transitional::video {

MwAdapter::MwAdapter(MwCoreOptions o, FrameSink sink) : core_(std::move(o)), sink_(std::move(sink)) {}

bool MwAdapter::playing() const { return handle_ && (core_.state() == kMwPrep || core_.state() == kMwPlaying); }

MwOutcome MwAdapter::create(GuestPort& g) {
    if (handle_) {  // um player nativo ja vivo: o jogo toca um filme por vez (nao observado o contrario)
        ++stats_.createFailures;
        return {false, 0};
    }
    const auto base = g.load32(kMwPoolPtr);
    if (!base) {
        ++stats_.createFailures;
        return {false, 0};
    }
    for (uint32_t i = 0; i < kMwEntries; ++i) {  // a mesma busca do original: entrada com +4 livre
        const uint32_t e = *base + 4 + i * kMwEntrySize;
        const auto     used = g.load32(e + 4);
        if (!used || *used != 0) continue;
        // entrada zerada (o original zera +8, +64, +68, +72, +100 e grava os subordinados em +52..+60 e
        // +88): com +64 = 0 o servidor por handle (8C1588D8) passa por ela sem efeito
        bool ok = true;
        for (uint32_t off = 0; off < kMwEntrySize && ok; off += 4) ok = g.store32(e + off, 0);
        if (!ok || !g.store32(e, kMwVtable) || !g.store32(e + 4, 1)) break;
        handle_ = e;
        ++stats_.creates;
        return {false, e};
    }
    ++stats_.createFailures;  // pool cheio (ou memoria fora da RAM): 0, como o original
    return {false, 0};
}

MwOutcome MwAdapter::call(uint32_t entry, const MwArgs& a, GuestPort& g) {
    const bool ours = handle_ && a.r4 == handle_;
    switch (entry) {
    case kEntCreate: return create(g);
    case kEntParam: return {!ours, 0};  // do nativo: sem efeito; de outro player: o original grava no objeto (8C192164)
    case kEntBase:
        if (ours) core_.set_base(a.r5 & 0xFF);
        return {!ours, 0};
    case kEntOffset:
        if (ours) core_.set_offset(a.r5 & 0xFF);
        return {!ours, 0};
    default: break;
    }
    if (!ours) {  // metodo de outro player da biblioteca
        ++stats_.foreign;
        return {true, 0};
    }
    switch (entry) {
    case kEntStart:
        g.store32(handle_ + 8, uint32_t(core_.start(g.cstring(a.r5, 64).value_or(""), g.now100ns())));
        return {false, 0};
    case kEntStat: {
        const int32_t r = core_.status(g.now100ns());
        g.store32(handle_ + 8, uint32_t(r));  // o estado fica em +8, como no original (8C1582C0 o le)
        if (const auto sub = g.load32(handle_ + 64); sub && *sub) ++stats_.serverExposed;
        native::video::VideoFrame f;
        if (sink_ && core_.frame(f)) sink_(f);
        return {false, uint32_t(r)};
    }
    case kEntStop:
        core_.stop();
        g.store32(handle_ + 8, kMwStop);
        return {false, 0};
    case kEntDestroy:
        core_.stop();
        g.store32(handle_ + 8, kMwStop);
        g.store32(handle_ + 4, 0);  // entrada livre no pool
        handle_ = 0;
        ++stats_.destroys;
        return {false, 0};
    default: return {true, 0};
    }
}

} // namespace bs::transitional::video
