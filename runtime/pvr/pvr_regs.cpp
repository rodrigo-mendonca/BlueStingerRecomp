// Registradores do PowerVR (Holly). Layout segundo a documentacao de registradores
// do KallistiOS (referencia de comportamento; nenhum codigo copiado):
//   ID         0x005F8000: 0x17FD11DB (so leitura)
//   REVISION   0x005F8004: 0x00000011 (so leitura); valores lidos pelo boot do jogo
//                          (8C1C59D6) no Flycast, que decide o caminho pela revisao
//   SPG_VBLANK_INT 0x005F80CC: bits 25-16 linha do VBlank-Out, 9-0 do VBlank-In
//   SPG_LOAD   0x005F80D8: bits 25-16 linhas por campo - 1
//   SPG_VBLANK 0x005F80DC: bits 25-16 fim (vbend), bits 9-0 inicio (vstart) do vblank
//   SPG_CONTROL 0x005F80D0: bit 4 entrelacado
//   SPG_STATUS 0x005F810C: bits 9-0 linha atual, 10 campo, 13 vsync (so leitura)
// (Ate 2026-10-14 os tres primeiros estavam em 0x0C4/0x0CC/0x0C8, enderecos errados;
// achado comparando o banco de registradores com o Flycast.)
//
// Modelo de tempo (aproximado, R1 do ARCHITECTURE.md): o campo dura 1/59.94 s de
// CPU a 200 MHz, dividido igualmente entre as linhas. O bit 13 de SPG_STATUS fica
// ligado de vstart ate vbend (o mesmo que o Flycast faz). Os ciclos do codigo gerado
// sao estimados, entao a fase exata nunca coincide com o Flycast.
#include "pvr/pvr_regs.h"

#include "pvr/holly_intc.h"
#include "pvr/ta.h"
#include "cpu/sh4_runtime.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace bs {
namespace {

constexpr uint32_t kBase      = 0x005F8000;
constexpr uint32_t kSize      = 0x2000;
constexpr uint32_t kId          = 0x005F8000;
constexpr uint32_t kRevision    = 0x005F8004;
constexpr uint32_t kSpgVblankInt = 0x005F80CC;
constexpr uint32_t kSpgVblank = 0x005F80DC;
constexpr uint32_t kSpgControl = 0x005F80D0;
constexpr uint32_t kSpgLoad   = 0x005F80D8;
constexpr uint32_t kSpgStatus = 0x005F810C;
constexpr uint32_t kTaListInit = 0x005F8144;  // bit 31: inicia listas novas
constexpr uint32_t kTaListCont = 0x005F8160;  // bit 31: continua as listas

constexpr uint64_t kCpuHz          = 200000000;
constexpr uint64_t kFieldCycles    = kCpuHz * 1001 / 60000;  // NTSC 59.94 Hz
constexpr uint32_t kDefaultLines   = 263;                    // NTSC nao entrelacado

std::array<uint32_t, kSize / 4> g_regs{};
std::array<bool, kSize / 4>     g_reported{};
// Fase do feixe no ciclo 0 (em ciclos de CPU, dentro de dois campos): vem do SPG_STATUS
// deixado pelo BIOS (linha + campo), carregado com o banco de registradores.
uint64_t                        g_phase = 0;

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
    g_phase = 0;
    g_reported.fill(false);
}

uint32_t pvr_vblank_in_line() { return reg(kSpgVblankInt) & 0x3FF; }
uint32_t pvr_vblank_out_line() { return (reg(kSpgVblankInt) >> 16) & 0x3FF; }

bool pvr_spg_crossed(uint64_t from, uint64_t to, uint32_t line) {
    if (to <= from) return false;
    from += g_phase;
    to += g_phase;
    const uint32_t lines = fieldLines();
    if (line >= lines) return false;  // linha que o campo nunca alcanca
    const uint64_t off = uint64_t(line) * kFieldCycles / lines;
    // eventos em off + k*campo; existe algum em (from, to]?
    auto count = [&](uint64_t t) { return t < off ? 0ull : (t - off) / kFieldCycles + 1; };
    return count(to) > count(from);
}

SpgPos pvr_spg_position(uint64_t cycles) {
    cycles += g_phase;
    const uint32_t lines   = fieldLines();
    const uint32_t vbstart = reg(kSpgVblank) & 0x3FF;
    const uint32_t vbend   = (reg(kSpgVblank) >> 16) & 0x3FF;

    SpgPos p;
    p.line  = uint32_t((cycles % kFieldCycles) * lines / kFieldCycles);
    p.vsync = vbstart > vbend ? (p.line >= vbstart || p.line < vbend) : (p.line >= vbstart && p.line < vbend);
    p.blank = p.vsync;
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
        // bit 10: campo (alterna a cada campo com SPG_CONTROL.interlace, bit 4); bit 11
        // nunca aceso no Flycast
        const bool interlace = reg(kSpgControl) & 0x10;
        const bool field     = interlace && (((c->cycles + g_phase) / kFieldCycles) & 1);
        v = (s.line & 0x3FF) | (field ? 1u << 10 : 0) | (s.vsync ? 1u << 13 : 0);
        return true;
    }
    v = reg(p);
    if (!g_reported[(p - kBase) / 4]) {  // latch sem modelo proprio: registrar (ver holly_sb.cpp)
        g_reported[(p - kBase) / 4] = true;
        std::fprintf(stderr, "[pvr] leitura sem modelo: %08X -> %08X (ultimo valor escrito)\n", p, v);
    }
    return true;
}

void pvr_regs_load(const uint32_t* words) {
    for (uint32_t i = 0; i < kSize / 4; ++i) g_regs[i] = words[i];
    const uint32_t status = words[(kSpgStatus - kBase) / 4];
    const uint32_t lines  = fieldLines();
    const uint32_t line   = std::min<uint32_t>(status & 0x3FF, lines - 1);
    g_phase = ((status >> 10) & 1) * kFieldCycles + (uint64_t(line) * kFieldCycles + lines - 1) / lines;
}

void pvr_regs_dump(Sh4Context* c, uint32_t* out) {
    for (uint32_t i = 0; i < kSize / 4; ++i) {
        const uint32_t p = kBase + 4 * i;
        if (p == kId || p == kRevision || p == kSpgStatus) pvr_reg_read(c, p, 4, out[i]);
        else out[i] = g_regs[i];  // sem o registro de "leitura sem modelo"
    }
}

bool pvr_reg_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    // Diagnostico: BS_PVR_DEBUG=1 registra a 1a escrita de cada registrador e todo STARTRENDER
    static const bool debug = std::getenv("BS_PVR_DEBUG") != nullptr;
    static std::array<bool, kSize / 4> seen{};
    if (debug && (!seen[(p - kBase) / 4] || p == 0x005F8014)) {
        seen[(p - kBase) / 4] = true;
        std::fprintf(stderr, "[pvr] escrita %08X = %08X (ciclo %llu)\n", p, v, (unsigned long long)c->cycles);
    }
    // Testes de integracao: BS_STOP_ON_STARTRENDER=<n> para na n-esima escrita em STARTRENDER
    if (p == 0x005F8014) {
        static const char* stop = std::getenv("BS_STOP_ON_STARTRENDER");
        static int         n    = 0;
        if (stop && ++n == std::atoi(stop)) sh4_fatal(c, "parada pedida: STARTRENDER", c->pc, uint32_t(n));
    }
    if (p == kSpgLoad || p == kSpgVblankInt) holly_intc_sync(c->cycles);
    if (p == kTaListInit && (v & 0x80000000)) ta_list_init();
    if (p == kTaListCont && (v & 0x80000000)) ta_list_continue();
    if (p != kSpgStatus && p != kId && p != kRevision) g_regs[(p - kBase) / 4] = v;
    return true;
}

} // namespace bs
