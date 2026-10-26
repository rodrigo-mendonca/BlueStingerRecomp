#include "memory/sys_logical.h"

#include "cpu/time_hooks.h"
#include "memory/sh4_ccn.h"
#include "memory/sh4_port.h"

namespace bs {
namespace {

constexpr uint32_t kCcr = 0xFF00001C, kPctra = 0xFF80002C, kPdtra = 0xFF800030;
constexpr uint32_t kCcrWriteOnly = (1u << 11) | (1u << 3);  // ICI, OCI
constexpr uint16_t kCablePins    = 0x0300;

struct State {
    uint32_t ccr      = 0;
    bool     ccnOut   = false;  // o modelo do CCN assumiu
    bool     portOut  = false;  // o modelo da porta assumiu
    uint64_t fallbacks = 0, served = 0;
} g;

bool logical() { return !time_provider().cycle_cadence(); }
bool cacheArray(uint32_t a) { return a >= 0xF0000000 && a < 0xF8000000; }
// as mesmas faixas que os modelos tratam (P4 e o espelho da area 7)
bool ccnRange(uint32_t a) { return a == kCcr || a == (kCcr & 0x1FFFFFFF) || cacheArray(a); }
bool portRange(uint32_t a) {
    const uint32_t p = a & 0x1FFFFFFF;
    return (a >= 0xE0000000 || (a >> 29) == 0) && (p == (kPctra & 0x1FFFFFFF) || p == (kPdtra & 0x1FFFFFFF));
}

bool ccnOut() {
    if (!g.ccnOut) sh4_ccn_adopt(g.ccr), g.ccnOut = true;
    ++g.fallbacks;
    return false;
}
bool portOut() {
    if (!g.portOut) sh4_port_adopt(0, 0), g.portOut = true;  // PCTRA e PDTRA nunca escritos no contrato
    ++g.fallbacks;
    return false;
}

} // namespace

bool sys_logical_read(uint32_t a, unsigned size, uint32_t& v) {
    if (!logical()) return false;
    if (ccnRange(a)) {
        if (g.ccnOut) return ++g.fallbacks, false;
        if (a == kCcr && size == 4) v = g.ccr;
        else if (cacheArray(a) && size == 4) v = 0;  // linha invalida (V = 0); o array so tem acesso de 32 bits
        else return ccnOut();
    } else if (portRange(a)) {
        if (g.portOut) return ++g.fallbacks, false;
        if (a == kPdtra && size == 2) v = uint16_t(kCablePins & (uint32_t(sh4_port_cable()) << 8));
        else return portOut();
    } else {
        return false;
    }
    ++g.served;
    return true;
}

bool sys_logical_write(uint32_t a, unsigned size, uint32_t v) {
    if (!logical()) return false;
    if (ccnRange(a)) {
        if (g.ccnOut) return ++g.fallbacks, false;
        if (a == kCcr && size == 4) g.ccr = v & ~kCcrWriteOnly;
        else if (!(cacheArray(a) && size == 4)) return ccnOut();
    } else if (portRange(a)) {
        return g.portOut ? (++g.fallbacks, false) : portOut();
    } else {
        return false;
    }
    ++g.served;
    return true;
}

void sys_logical_reset() {
    const uint64_t f = g.fallbacks, s = g.served;
    g           = State{};
    g.fallbacks = f;
    g.served    = s;
}

uint64_t sys_logical_fallbacks() { return g.fallbacks; }
uint64_t sys_logical_served() { return g.served; }

} // namespace bs
