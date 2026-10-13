// Registradores do PowerVR (Holly). Layout segundo a documentacao de registradores
// do KallistiOS (referencia de comportamento; nenhum codigo copiado):
//   ID         0x005F8000: 0x17FD11DB (so leitura)
//   REVISION   0x005F8004: 0x00000011 (so leitura); valores lidos pelo boot do jogo
//                          (8C1C59D6) no Flycast, que decide o caminho pela revisao
//   SPG_VBLANK_INT 0x005F80C4: bits 25-16 linha do VBlank-Out, 9-0 do VBlank-In
//   SPG_WIDTH  0x005F80C8: bits 31-28 largura do vsync em linhas
//   SPG_VBLANK 0x005F80CC: bits 25-16 fim do blank, bits 9-0 inicio do blank
//   SPG_LOAD   0x005F80D8: bits 25-16 linhas por campo - 1
//   SPG_STATUS 0x005F810C: bits 9-0 linha atual, 11 blank, 13 vsync (so leitura)
//
// Modelo de tempo (aproximado, R1 do ARCHITECTURE.md): o campo dura 1/59.94 s de
// CPU a 200 MHz, dividido igualmente entre as linhas; a linha 0 comeca o vsync.
// Os ciclos do codigo gerado sao estimados, entao a fase exata nunca coincide com
// o Flycast; o que se preserva e a sequencia vsync -> fim do vsync -> proximo campo.
#include "pvr/pvr_regs.h"

#include "pvr/holly_intc.h"

#include <array>
#include <cstdio>

namespace bs {
namespace {

constexpr uint32_t kBase      = 0x005F8000;
constexpr uint32_t kSize      = 0x2000;
constexpr uint32_t kId          = 0x005F8000;
constexpr uint32_t kRevision    = 0x005F8004;
constexpr uint32_t kSpgVblankInt = 0x005F80C4;
constexpr uint32_t kSpgWidth  = 0x005F80C8;
constexpr uint32_t kSpgVblank = 0x005F80CC;
constexpr uint32_t kSpgLoad   = 0x005F80D8;
constexpr uint32_t kSpgStatus = 0x005F810C;

constexpr uint64_t kCpuHz          = 200000000;
constexpr uint64_t kFieldCycles    = kCpuHz * 1001 / 60000;  // NTSC 59.94 Hz
constexpr uint32_t kDefaultLines   = 263;                    // NTSC nao entrelacado
constexpr uint32_t kDefaultVsWidth = 3;

std::array<uint32_t, kSize / 4> g_regs{};
std::array<bool, kSize / 4>     g_reported{};

uint32_t reg(uint32_t a) { return g_regs[(a - kBase) / 4]; }

// Area 0 vista por P0-P3 (o jogo usa P2, 0xA05F....).
uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }

uint32_t fieldLines() {
    const uint32_t lines = ((reg(kSpgLoad) >> 16) & 0x3FF) + 1;
    return lines < 2 ? kDefaultLines : lines;
}

} // namespace

void pvr_regs_reset() {
    g_regs.fill(0);
    g_reported.fill(false);
}

uint32_t pvr_vblank_in_line() { return reg(kSpgVblankInt) & 0x3FF; }
uint32_t pvr_vblank_out_line() { return (reg(kSpgVblankInt) >> 16) & 0x3FF; }

bool pvr_spg_crossed(uint64_t from, uint64_t to, uint32_t line) {
    if (to <= from) return false;
    const uint32_t lines = fieldLines();
    if (line >= lines) return false;  // linha que o campo nunca alcanca
    const uint64_t off = uint64_t(line) * kFieldCycles / lines;
    // eventos em off + k*campo; existe algum em (from, to]?
    auto count = [&](uint64_t t) { return t < off ? 0ull : (t - off) / kFieldCycles + 1; };
    return count(to) > count(from);
}

SpgPos pvr_spg_position(uint64_t cycles) {
    const uint32_t lines = fieldLines();
    uint32_t vsw = reg(kSpgWidth) >> 28;
    if (vsw == 0) vsw = kDefaultVsWidth;
    const uint32_t vbstart = reg(kSpgVblank) & 0x3FF;
    const uint32_t vbend   = (reg(kSpgVblank) >> 16) & 0x3FF;

    SpgPos p;
    p.line  = uint32_t((cycles % kFieldCycles) * lines / kFieldCycles);
    p.vsync = p.line < vsw;
    p.blank = vbstart > vbend ? (p.line >= vbstart || p.line < vbend) : (p.line >= vbstart && p.line < vbend);
    return p;
}

bool pvr_reg_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    if (p == kId) {
        v = 0x17FD11DB;
        return true;
    }
    if (p == kRevision) {
        v = 0x00000011;
        return true;
    }
    if (p == kSpgStatus) {
        const SpgPos s = pvr_spg_position(c->cycles);
        v = (s.line & 0x3FF) | (s.blank ? 1u << 11 : 0) | (s.vsync ? 1u << 13 : 0);
        return true;
    }
    v = reg(p);
    if (!g_reported[(p - kBase) / 4]) {  // latch sem modelo proprio: registrar (ver holly_sb.cpp)
        g_reported[(p - kBase) / 4] = true;
        std::fprintf(stderr, "[pvr] leitura sem modelo: %08X -> %08X (ultimo valor escrito)\n", p, v);
    }
    return true;
}

bool pvr_reg_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    if (p == kSpgLoad || p == kSpgVblankInt) holly_intc_sync(c->cycles);
    if (p != kSpgStatus && p != kId && p != kRevision) g_regs[(p - kBase) / 4] = v;
    return true;
}

} // namespace bs
