#include "render/kamui/frame_state.h"

#include <algorithm>

namespace bs::native::render::kamui {
namespace {
constexpr uint32_t kDefaultLines = 263;  // NTSC nao entrelacado
} // namespace

void FrameState::reset() {
    regs_.fill(0);
    ++pal_gen_;
    phase_ = 0;
}

uint32_t FrameState::field_lines() const {
    const uint32_t lines = ((reg(kSpgLoad) >> 16) & 0x3FF) + 1;
    return lines < 2 ? kDefaultLines : lines;
}

void FrameState::load(const uint32_t* words) {
    for (uint32_t i = 0; i < kSize / 4; ++i) regs_[i] = words[i];
    ++pal_gen_;
    const uint32_t status = words[(kSpgStatus - kBase) / 4];
    const uint32_t lines  = field_lines();
    const uint32_t line   = std::min<uint32_t>(status & 0x3FF, lines - 1);
    phase_ = ((status >> 10) & 1) * field_ + (uint64_t(line) * field_ + lines - 1) / lines;
}

uint32_t FrameState::read(uint32_t p, uint64_t now, bool& modeled) const {
    modeled = true;
    if (p == kId) return 0x17FD11DB;
    if (p == kRevision) return 0x00000011;
    if (p == kSpgStatus) {
        const SpgPosition s = spg_position(now);
        // bit 10: campo (alterna a cada campo com SPG_CONTROL.interlace, bit 4); bit 11 nunca aceso no Flycast
        const bool interlace = reg(kSpgControl) & 0x10;
        const bool field     = interlace && (((now + phase_) / field_) & 1);
        return (s.line & 0x3FF) | (field ? 1u << 10 : 0) | (s.vsync ? 1u << 13 : 0);
    }
    modeled = false;
    return reg(p);
}

void FrameState::write(uint32_t p, uint32_t v, FrameEffects& fx) {
    if (p == kStartRender) fx.start_render();
    if (p == kSpgLoad || p == kSpgVblankInt) fx.spg_sync();
    if (p == kTaListInit && (v & 0x80000000)) {
        fx.list_init();
        // o TA recomeca nos ponteiros iniciais; o jogo le TA_ITP_CURRENT para saber onde gravar o poligono de fundo
        // (aproximacao: no hardware ele avanca com os parametros gravados; o Flycast, o oraculo, nao o avanca)
        regs_[(kTaNextOpb - kBase) / 4]    = reg(kTaNextOpbInit);
        regs_[(kTaItpCurrent - kBase) / 4] = reg(kTaIspBase);
    }
    if (p == kTaListCont) fx.list_continue();  // qualquer escrita (como no Flycast)
    // registradores de comando e so de leitura nao guardam o valor escrito
    const bool command  = p == kTaListInit || p == kStartRender || p == kSoftReset;
    const bool readOnly = p == kSpgStatus || p == kId || p == kRevision || p == kTaYuvTexCnt;
    if (!command && !readOnly) regs_[(p - kBase) / 4] = v;
    if (p == kPalRamCtrl || (p >= kPalette && p < kPalette + 0x1000)) ++pal_gen_;
    if (p == kSpgLoad || p == kSpgVblankInt) fx.spg_changed();
    if (p == kTaYuvTexBase || p == kTaYuvTexCtrl) fx.yuv(reg(kTaYuvTexBase), reg(kTaYuvTexCtrl));
}

// PAL_RAM_CTRL bits 1-0: 0 ARGB1555, 1 RGB565, 2 ARGB4444, 3 ARGB8888
uint32_t FrameState::palette_argb(uint32_t index) const {
    const uint32_t e   = reg(kPalette + 4 * (index & 1023));
    const uint32_t fmt = reg(kPalRamCtrl) & 3;
    if (fmt == 3) return e;
    auto x = [](uint32_t v, int bits) {
        v <<= 8 - bits;
        return v | (v >> bits);
    };
    uint32_t a, r, g, b;
    if (fmt == 0) a = (e & 0x8000) ? 0xFF : 0, r = x((e >> 10) & 31, 5), g = x((e >> 5) & 31, 5), b = x(e & 31, 5);
    else if (fmt == 1) a = 0xFF, r = x((e >> 11) & 31, 5), g = x((e >> 5) & 63, 6), b = x(e & 31, 5);
    else a = x((e >> 12) & 15, 4), r = x((e >> 8) & 15, 4), g = x((e >> 4) & 15, 4), b = x(e & 15, 4);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

bool FrameState::spg_crossed(uint64_t from, uint64_t to, uint32_t line) const {
    if (to <= from) return false;
    from += phase_;
    to += phase_;
    const uint32_t lines = field_lines();
    if (line >= lines) return false;  // linha que o campo nunca alcanca
    const uint64_t off = uint64_t(line) * field_ / lines;
    // eventos em off + k*campo; existe algum em (from, to]?
    auto count = [&](uint64_t t) { return t < off ? 0ull : (t - off) / field_ + 1; };
    return count(to) > count(from);
}

uint64_t FrameState::spg_next_crossing(uint64_t after, uint32_t line) const {
    const uint32_t lines = field_lines();
    if (line >= lines) return kNever;
    const uint64_t off = uint64_t(line) * field_ / lines;
    const uint64_t x   = after + phase_;
    if (x < off) return off - phase_;
    const uint64_t k = (x - off) / field_ + 1;
    return off + k * field_ - phase_;
}

uint64_t FrameState::spg_next_line_change(uint64_t after) const {
    // a linha muda na primeira posicao do campo com (posicao * linhas / campo) >= linha + 1
    const uint32_t lines = field_lines();
    const uint64_t x     = after + phase_;
    const uint64_t base  = x - x % field_;
    const uint32_t line  = uint32_t((x % field_) * lines / field_);
    const uint64_t next  = line + 1 >= lines ? field_ : (uint64_t(line + 1) * field_ + lines - 1) / lines;
    return base + next - phase_;
}

SpgPosition FrameState::spg_position(uint64_t t) const {
    t += phase_;
    const uint32_t lines   = field_lines();
    const uint32_t vbstart = reg(kSpgVblank) & 0x3FF;
    const uint32_t vbend   = (reg(kSpgVblank) >> 16) & 0x3FF;
    SpgPosition    p;
    p.line  = uint32_t((t % field_) * lines / field_);
    p.vsync = vbstart > vbend ? (p.line >= vbstart || p.line < vbend) : (p.line >= vbstart && p.line < vbend);
    p.blank = p.vsync;
    return p;
}

} // namespace bs::native::render::kamui
