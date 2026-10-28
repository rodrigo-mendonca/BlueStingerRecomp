// N8.6: area de som do produto sem AICA (ver sound_bus.h e docs/N8-6-AUDIO.md).
#include "system/sound_bus.h"

#include "cpu/sh4_runtime.h"
#include "cpu/time_hooks.h"
#include "debug/clock_log.h"
#include "debug/intc_trace.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace bs {
namespace {

constexpr uint32_t kRamSize = 2u << 20;
constexpr uint32_t kCtrl = 0x00702C00, kRtcHi = 0x00710000, kRtcLo = 0x00710004, kRtcEn = 0x00710008;

std::vector<uint8_t>   g_ram(kRamSize);
uint32_t               g_ctrl      = 0;
bool                   g_rtcEnable = false;
bool                   g_rtcSet    = false;  // sem valor definido: hora do host no primeiro acesso
uint64_t               g_refused   = 0;
const SoundBusBackend* g_backend   = nullptr;

uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }
bool     inRam(uint32_t p) { return p >= 0x00800000 && p < 0x01000000; }
bool     inRegs(uint32_t p) { return p >= 0x00700000 && p < 0x00720000; }

[[noreturn]] void removed(Sh4Context* c, bool write, uint32_t p, unsigned size, uint32_t v) {
    ++g_refused;
    static std::string msg;
    char               buf[224];
    std::snprintf(buf, sizeof buf,
                  "%s de %u bytes em %08X fora do contrato da area de som: AICA, ARM7 e driver de som foram removidos "
                  "do produto no N8.6 (o som e nativo; o modelo so existe no build de validacao)",
                  write ? "escrita" : "leitura", size, p);
    msg = buf;
    std::fprintf(stderr, "[som] %s\n", msg.c_str());
    sh4_fatal(c, msg.c_str(), c->pc, v);
}

void nativeRtcSet(uint32_t seconds, uint64_t now) {
    time_provider().rtc_set(seconds, now);
    g_rtcSet = true;
}

uint32_t rtcNow(uint64_t now) {
    if (!g_rtcSet) nativeRtcSet(snd_rtc_host_now(), now);
    return time_provider().rtc_seconds(now);
}

void nativeReset() {
    std::fill(g_ram.begin(), g_ram.end(), uint8_t(0));
    g_ctrl      = 0;
    g_rtcEnable = false;
    nativeRtcSet(snd_rtc_host_now(), 0);
}

bool nativeRead(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (inRam(p)) {
        v = 0;
        std::memcpy(&v, &g_ram[p & (kRamSize - 1)], size);
        if (g_clock_log) clock_log_read(ClockRead::AudRd, p), clock_log_read(ClockRead::AudRd, v);
        return true;
    }
    if (!inRegs(p)) return false;
    if (size != 4) removed(c, false, p, size, 0);
    if (p == kCtrl) v = g_ctrl;
    else if (p == kRtcHi) v = rtcNow(time_now(c)) >> 16;
    else if (p == kRtcLo) v = rtcNow(time_now(c)) & 0xFFFF;
    else if (p == kRtcEn) v = 0;
    else removed(c, false, p, size, 0);
    if (g_clock_log && (p == kRtcHi || p == kRtcLo)) clock_log_read(ClockRead::Rtc, v);
    if (g_intc_trace && (p == kRtcHi || p == kRtcLo)) intc_trace_clock(IntcSource::Rtc, v);
    return true;
}

bool nativeWrite(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (inRam(p)) {
        std::memcpy(&g_ram[p & (kRamSize - 1)], &v, size);
        if (g_clock_log) clock_log_read(ClockRead::Aud, p), clock_log_read(ClockRead::Aud, v);
        return true;
    }
    if (!inRegs(p)) return false;
    if (size != 4) removed(c, true, p, size, v);
    if (p != kCtrl && p != kRtcEn && p != kRtcHi && p != kRtcLo) removed(c, true, p, size, v);
    if (g_clock_log) clock_log_read(ClockRead::Aud, p), clock_log_read(ClockRead::Aud, v);
    if (p == kCtrl) {
        g_ctrl = v;
    } else if (p == kRtcEn) {
        g_rtcEnable = v & 1;
    } else if (g_rtcEnable) {  // RTC: altos ou baixos, com a escrita liberada
        const uint32_t now = rtcNow(time_now(c));
        const uint32_t val = p == kRtcHi ? (now & 0xFFFF) | ((v & 0xFFFF) << 16) : (now & 0xFFFF0000) | (v & 0xFFFF);
        nativeRtcSet(val, time_now(c));
        if (p == kRtcHi) g_rtcEnable = false;
    }
    return true;
}

} // namespace

uint32_t snd_rtc_host_now() {
    const std::time_t now = std::time(nullptr);
    std::tm           local{}, utc{};
#ifdef _WIN32
    localtime_s(&local, &now);
    gmtime_s(&utc, &now);
#else
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
#endif
    const std::time_t offset    = std::mktime(&local) - std::mktime(&utc);  // fuso + horario de verao
    constexpr int64_t epoch1950 = int64_t(20 * 365 + 5) * 24 * 60 * 60;    // 1950 -> 1970
    return uint32_t(epoch1950 + int64_t(now) + int64_t(offset));
}

void snd_bus_use(const SoundBusBackend* b) { g_backend = b; }
void snd_bus_reset() { g_backend ? g_backend->reset() : nativeReset(); }
void snd_bus_rtc_set(uint32_t seconds, uint64_t now) { g_backend ? g_backend->rtc_set(seconds, now) : nativeRtcSet(seconds, now); }
bool snd_bus_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    return g_backend ? g_backend->read(c, a, size, v) : nativeRead(c, a, size, v);
}
bool snd_bus_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    return g_backend ? g_backend->write(c, a, size, v) : nativeWrite(c, a, size, v);
}
uint8_t* snd_bus_ram() { return g_backend ? g_backend->ram() : g_ram.data(); }
uint64_t sound_bus_refused() { return g_refused; }

} // namespace bs
