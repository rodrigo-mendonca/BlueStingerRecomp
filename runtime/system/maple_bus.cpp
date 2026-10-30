// N8.4: barramento de perifericos sem dispositivos (ver maple_bus.h e docs/N8-4-PERIFERICOS.md).
#include "system/maple_bus.h"

#include "cpu/sh4_runtime.h"
#include "cpu/time_hooks.h"
#include "debug/clock_log.h"
#include "debug/intc_trace.h"
#include "input/input.h"
#include "pvr/holly_intc.h"
#include "system/graphics_ports.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace bs {
namespace {

constexpr uint32_t kMdstar = 0x005F6C04, kMdtsel = 0x005F6C10, kMden = 0x005F6C14, kMdst = 0x005F6C18;
constexpr uint32_t kBase = 0x005F6C00, kSize = 0x100;
constexpr uint8_t  kCmdDevInfo = 0x01;

std::array<uint32_t, kSize / 4> g_regs{};
uint64_t                        g_due = 0;
bool                            g_transitionArmed = false;  // o primeiro acesso ao MDST depois do VBlank-In avanca a transicao
MapleCounters                   g_counters;
uint64_t                        g_refused = 0;
const PeriphBackend*            g_backend = nullptr;

uint32_t& reg(uint32_t p) { return g_regs[(p - kBase) / 4]; }

[[noreturn]] void removed(Sh4Context* c, const char* what, uint32_t value) {
    ++g_refused;
    static std::string msg;
    char               buf[224];
    std::snprintf(buf, sizeof buf,
                  "barramento de perifericos: %s (%08X) fora do contrato do servico nativo: o dispositivo ou o modo que "
                  "o atenderia (controle, VMU, Maple simulado) foi removido do produto no N8.4",
                  what, value);
    msg = buf;
    std::fprintf(stderr, "[perifericos] %s\n", msg.c_str());
    sh4_fatal(c, msg.c_str(), c->pc, value);
}

uint8_t* ramAt(Sh4Context* c, uint32_t phys, uint32_t bytes) {
    phys &= 0x1FFFFFFF;
    if ((phys >> 26) != 3 || (phys & 0x00FFFFFF) + bytes > (16u << 20)) sh4_fatal(c, "DMA do Maple fora da RAM", c->pc, phys);
    return c->ram + (phys & 0x00FFFFFF);
}

uint32_t rd(Sh4Context* c, uint32_t phys) {
    uint32_t v;
    std::memcpy(&v, ramAt(c, phys, 4), 4);
    return v;
}

// A tabela de descritores do jogo: cada porta, DEVICE_INFO ao dispositivo principal; ninguem responde.
void runDma(Sh4Context* c) {
    uint32_t addr = reg(kMdstar) & 0x1FFFFFE0;
    uint64_t bits = 0;
    ++g_counters.dmas;
    for (int n = 0;; ++n) {
        if (n == 1024) sh4_fatal(c, "Maple: tabela sem descritor final (LAST)", c->pc, reg(kMdstar));
        const uint32_t instr = rd(c, addr), recv = rd(c, addr + 4);
        const uint32_t port = (instr >> 16) & 3, pattern = (instr >> 8) & 7, len = instr & 0xFF;
        if (pattern != 0) removed(c, "padrao de transferencia", instr);
        const uint32_t hdr = rd(c, addr + 8);
        const uint8_t  cmd = hdr & 0xFF, dst = (hdr >> 8) & 0xFF;
        if (cmd != kCmdDevInfo) removed(c, "comando", hdr);
        if (dst != ((port << 6) | 0x20)) removed(c, "destino", hdr);
        if (len != 0) removed(c, "quadro com dados", instr);
        ++g_counters.devInfo[port];
        std::memset(ramAt(c, recv, 4), 0xFF, 4);  // sem dispositivo: sem resposta
        bits += (1 + len + 1) * 32ull;
        addr += 12 + 4 * len;
        if (instr & 0x80000000) break;
    }
    g_due = time_now(c) + bits * 100;
    if (g_due == 0) g_due = 1;
    time_provider().maple_scheduled(g_due);
}

void nativeReset() {
    g_regs.fill(0);
    g_due = 0;
    time_provider().maple_cancel();
    g_transitionArmed = false;
    g_counters        = {};
}

bool nativeRead(Sh4Context* c, uint32_t a, uint32_t& v) {
    const uint32_t p = a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0;
    if (p < kBase || p >= kBase + kSize) return false;
    v = reg(p);
    if (p == kMdst) {
        if (g_transitionArmed) g_transitionArmed = false, input_transition_tick();  // N3-0: transicao injetada
        v = (g_due && time_now(c) < g_due) ? 1 : 0;
        if (g_clock_log) clock_log_read(ClockRead::Mdst, v);
        if (g_intc_trace) intc_trace_clock(IntcSource::Mdst, v);
    }
    return true;
}

bool nativeWrite(Sh4Context* c, uint32_t a, uint32_t v) {
    const uint32_t p = a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0;
    if (p < kBase || p >= kBase + kSize) return false;
    if (p == kMdtsel && (v & 1)) removed(c, "disparo por VBlank (MDTSEL)", v);
    if (p == kMdst) {
        if (g_due && time_now(c) >= g_due) {  // fim anterior ainda nao visto pelo controlador
            event_status_raise(kIstMapleDma);
            g_due = 0;
            time_provider().maple_cancel();
        }
        if ((v & 1) && (reg(kMden) & 1) && !g_due) runDma(c);
        return true;
    }
    reg(p) = v;
    return true;
}

void          nativeVblankIn() { g_transitionArmed = true; }
void          nativeTake() { g_due = 0; }
MapleCounters nativeCounters() { return g_counters; }
uint64_t      nativeCondSeq(int) { return 0; }

} // namespace

void periph_use(const PeriphBackend* b) { g_backend = b; }
void periph_reset() { g_backend ? g_backend->reset() : nativeReset(); }
bool periph_read(Sh4Context* c, uint32_t a, uint32_t& v) { return g_backend ? g_backend->read(c, a, v) : nativeRead(c, a, v); }
bool periph_write(Sh4Context* c, uint32_t a, uint32_t v) { return g_backend ? g_backend->write(c, a, v) : nativeWrite(c, a, v); }
void periph_vblank_in() { g_backend ? g_backend->vblank_in() : nativeVblankIn(); }
void periph_dma_take() { g_backend ? g_backend->dma_take() : nativeTake(); }
MapleCounters periph_counters() { return g_backend ? g_backend->counters() : nativeCounters(); }
uint64_t      periph_cond_seq(int port) { return g_backend ? g_backend->cond_seq(port) : nativeCondSeq(port); }
uint64_t      maple_bus_refused() { return g_refused; }

} // namespace bs
