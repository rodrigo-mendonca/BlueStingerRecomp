// AICA, registradores usados no boot (documentacao do KallistiOS; Flycast so como
// referencia de comportamento):
//   0x00800000  RAM de som, 2 MB (repetida ate 0x00FFFFFF); o boot a preenche com "SEGA"
//   0x00702C00  controle: bit 0 reset do ARM7, bits 8-9 saida de video. Guardado
//               (o boot faz leitura-modificacao-escrita dos bits 8-9).
//   0x00710000  RTC, 16 bits altos
//   0x00710004  RTC, 16 bits baixos
//   0x00710008  bit 0 libera a escrita do RTC; escrever os 16 bits altos a desliga
// O RTC conta segundos desde 1950-01-01 e avanca um por segundo de CPU emulada.
#include "aica/aica_regs.h"

#include <cstring>
#include <ctime>
#include <vector>

namespace bs {
namespace {

constexpr uint64_t kCpuHz   = 200000000;
constexpr uint32_t kCtrl    = 0x00702C00;
constexpr uint32_t kRtcHi   = 0x00710000;
constexpr uint32_t kRtcLo   = 0x00710004;
constexpr uint32_t kRtcEn   = 0x00710008;

uint32_t g_ctrl = 0;
uint32_t g_rtcBase = 0;       // valor do RTC em g_rtcCycles
uint64_t g_rtcCycles = 0;
bool     g_rtcEnable = false;
bool     g_rtcSet = false;        // sem valor definido: hora do host no primeiro acesso

std::vector<uint8_t> g_ram(kAicaRamSize);

uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }
bool     inRam(uint32_t p) { return p >= 0x00800000 && p < 0x01000000; }

uint32_t rtcNow(uint64_t cycles) {
    if (!g_rtcSet) aica_rtc_set(aica_rtc_host_now(), cycles);
    return g_rtcBase + uint32_t((cycles >= g_rtcCycles ? cycles - g_rtcCycles : 0) / kCpuHz);
}

} // namespace

uint8_t* aica_ram() { return g_ram.data(); }

void aica_regs_reset() {
    std::fill(g_ram.begin(), g_ram.end(), uint8_t(0));
    g_ctrl      = 0;
    g_rtcEnable = false;
    aica_rtc_set(aica_rtc_host_now(), 0);
}

void aica_rtc_set(uint32_t seconds, uint64_t cycles) {
    g_rtcBase   = seconds;
    g_rtcCycles = cycles;
    g_rtcSet    = true;
}

uint32_t aica_rtc_host_now() {
    const std::time_t now = std::time(nullptr);
    std::tm           local{}, utc{};
#ifdef _WIN32
    localtime_s(&local, &now);
    gmtime_s(&utc, &now);
#else
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
#endif
    const std::time_t offset = std::mktime(&local) - std::mktime(&utc);  // fuso + horario de verao
    constexpr int64_t epoch1950 = int64_t(20 * 365 + 5) * 24 * 60 * 60;  // 1950 -> 1970
    return uint32_t(epoch1950 + int64_t(now) + int64_t(offset));
}

bool aica_reg_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (inRam(p)) {
        v = 0;
        std::memcpy(&v, &g_ram[p & (kAicaRamSize - 1)], size);
        return true;
    }
    if (p == kCtrl && size == 4) v = g_ctrl;
    else if (p == kRtcHi && size == 4) v = rtcNow(c->cycles) >> 16;
    else if (p == kRtcLo && size == 4) v = rtcNow(c->cycles) & 0xFFFF;
    else if (p == kRtcEn && size == 4) v = 0;
    else return false;
    return true;
}

bool aica_reg_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (inRam(p)) {
        std::memcpy(&g_ram[p & (kAicaRamSize - 1)], &v, size);
        return true;
    }
    if (size != 4) return false;
    if (p == kCtrl) {
        g_ctrl = v;
    } else if (p == kRtcEn) {
        g_rtcEnable = v & 1;
    } else if (p == kRtcHi || p == kRtcLo) {
        if (g_rtcEnable) {
            const uint32_t now = rtcNow(c->cycles);
            const uint32_t val = p == kRtcHi ? (now & 0xFFFF) | ((v & 0xFFFF) << 16) : (now & 0xFFFF0000) | (v & 0xFFFF);
            aica_rtc_set(val, c->cycles);
            if (p == kRtcHi) g_rtcEnable = false;
        }
    } else {
        return false;
    }
    return true;
}

} // namespace bs
