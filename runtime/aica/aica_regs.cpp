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

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
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

// Diagnostico (BS_AICA_TRACE=1): acessos da CPU a RAM de som por endereco, com o primeiro e o
// ultimo valor e o ciclo; relatorio dos mais lidos no fim do processo. Serve para achar o que
// o jogo espera do driver de som (ARM7) que ainda nao roda.
namespace {
struct RamStat {
    uint64_t reads = 0, writes = 0, firstCycle = 0, lastCycle = 0;
    uint32_t firstValue = 0, lastValue = 0, changes = 0;
    uint32_t readers[4] = {};  // funcoes SH-4 que leram (as 4 primeiras)
};
uint32_t g_lastFn = 0;  // ultima funcao gerada que comecou (BS_FN_ENTER)
uint32_t g_pr     = 0;  // PR no momento da leitura (retorno dentro de quem leu)
void      onEnter(Sh4Context*, uint32_t entry) { g_lastFn = entry; }
std::map<uint32_t, RamStat>* g_trace = nullptr;
void traceReport() {
    if (!g_trace) return;
    std::vector<std::pair<uint32_t, RamStat>> v(g_trace->begin(), g_trace->end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.reads > b.second.reads; });
    size_t   other = 0;
    uint32_t lo = 0xFFFFFFFF, hi = 0;
    for (const auto& [addr, s] : v)
        if (s.writes && s.lastValue != 0x41474553) ++other, lo = std::min(lo, addr), hi = std::max(hi, addr);
    std::fprintf(stderr, "[aica] RAM de som: %zu enderecos acessados pela CPU; %zu escritos com valor diferente de "
                         "\"SEGA\" (%08X-%08X); mais lidos:\n",
                 v.size(), other, other ? lo : 0, hi);
    for (size_t i = 0; i < v.size() && i < 40; ++i) {
        const auto& [addr, s] = v[i];
        std::fprintf(stderr,
                     "[aica]   %08X leituras %llu escritas %llu valor %08X -> %08X (mudou %u vezes) ciclos %llu-%llu "
                     "PR na leitura %08X %08X %08X %08X\n",
                     addr, (unsigned long long)s.reads, (unsigned long long)s.writes, s.firstValue, s.lastValue,
                     s.changes, (unsigned long long)s.firstCycle, (unsigned long long)s.lastCycle, s.readers[0],
                     s.readers[1], s.readers[2], s.readers[3]);
    }
}
void trace(bool write, uint32_t p, uint32_t v, uint64_t cycles, uint32_t pr) {
    static const bool on = std::getenv("BS_AICA_TRACE") != nullptr;
    if (!on) return;
    if (!g_trace) {
        g_trace = new std::map<uint32_t, RamStat>();
        std::atexit(traceReport);
        if (!g_sh4_trace) sh4_set_trace(&onEnter);  // custo so com o diagnostico; nao tira outro observador
    }
    RamStat& s = (*g_trace)[p & ~3u];
    if (!s.reads && !s.writes) s.firstValue = v, s.firstCycle = cycles;
    if (s.reads + s.writes && v != s.lastValue) ++s.changes;
    (write ? s.writes : s.reads)++;
    // primeira escrita do driver (vetores do ARM em 0x00800000, diferente do preenchimento)
    static bool driverSeen = false;
    if (write && !driverSeen && (p & ~3u) == 0x00800000 && v != 0x41474553) {
        driverSeen = true;
        std::fprintf(stderr, "[aica] primeira escrita do driver em 00800000 = %08X (ciclo %llu, PR %08X, funcao %08X)\n", v,
                     (unsigned long long)cycles, pr, g_lastFn);
    }
    // BS_AICA_DUMP_AT=N: na N-esima leitura deste endereco, mostra o inicio da RAM de som e a
    // area lida (para comparar com o oraculo parado no mesmo ponto)
    static const long dumpAt = std::getenv("BS_AICA_DUMP_AT") ? std::atol(std::getenv("BS_AICA_DUMP_AT")) : 0;
    if (!write && dumpAt && s.reads == uint64_t(dumpAt) && (p & ~3u) == 0x00813400) {
        for (uint32_t base : {0x00800000u, 0x00813400u, 0x00813A00u}) {
            std::fprintf(stderr, "[aica] %08X:", base);
            for (uint32_t i = 0; i < 8; ++i) {
                uint32_t w;
                std::memcpy(&w, &g_ram[(base + 4 * i) & (kAicaRamSize - 1)], 4);
                std::fprintf(stderr, " %08X", w);
            }
            std::fprintf(stderr, "\n");
        }
    }
    g_pr = pr;
    if (!write)
        for (uint32_t& r : s.readers)
            if (r == g_pr || !r) {
                r = g_pr;
                break;
            }
    s.lastValue = v, s.lastCycle = cycles;
}
} // namespace

bool aica_reg_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (inRam(p)) {
        v = 0;
        std::memcpy(&v, &g_ram[p & (kAicaRamSize - 1)], size);
        trace(false, p, v, c->cycles, c->pr);
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
        trace(true, p, v, c->cycles, c->pr);
        return true;
    }
    if (size != 4) return false;
    if (p == kCtrl) {
        if (std::getenv("BS_AICA_TRACE"))  // diagnostico: reset/liberacao do ARM7 (bit 0)
            std::fprintf(stderr, "[aica] controle 00702C00 = %08X (ARM7 %s; ciclo %llu, PR %08X, funcao %08X)\n", v,
                         (v & 1) ? "em reset" : "liberado", (unsigned long long)c->cycles, c->pr, g_lastFn);
        if (std::getenv("BS_AICA_TRACE")) {  // pilha: enderecos de retorno salvos pelos chamadores
            std::fprintf(stderr, "[aica]   pilha (r15=%08X):", c->r[15]);
            for (uint32_t i = 0; i < 24; ++i) {
                const uint32_t w = sh4_read32(c, c->r[15] + 4 * i);
                if ((w & 0xFFF00000) == 0x8C000000) std::fprintf(stderr, " +%u:%08X", 4 * i, w);
            }
            std::fprintf(stderr, "\n");
        }
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
