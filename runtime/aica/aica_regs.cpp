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
#include "cpu/time_hooks.h"
#include "debug/clock_log.h"
#include "debug/intc_trace.h"
#include "pvr/holly_intc.h"

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

constexpr uint32_t kCtrl    = 0x00702C00;
constexpr uint32_t kRtcHi   = 0x00710000;
constexpr uint32_t kRtcLo   = 0x00710004;
constexpr uint32_t kRtcEn   = 0x00710008;

uint32_t g_ctrl = 0;
bool     g_rtcEnable = false;
bool     g_rtcSet = false;        // sem valor definido: hora do host no primeiro acesso

std::vector<uint8_t> g_ram(kAicaRamSize);

uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }
bool     inRam(uint32_t p) { return p >= 0x00800000 && p < 0x01000000; }

// N2a: a contagem do RTC e do provedor de tempo (legado: base + ciclos; nativo: relogio logico)
uint32_t rtcNow(uint64_t now) {
    if (!g_rtcSet) aica_rtc_set(aica_rtc_host_now(), now);
    return time_provider().rtc_seconds(now);
}

} // namespace

uint8_t* aica_ram() { return g_ram.data(); }

void aica_regs_reset() {
    std::fill(g_ram.begin(), g_ram.end(), uint8_t(0));
    g_ctrl      = 0;
    g_rtcEnable = false;
    aica_rtc_set(aica_rtc_host_now(), 0);
}

void aica_rtc_set(uint32_t seconds, uint64_t now) {
    time_provider().rtc_set(seconds, now);
    g_rtcSet = true;
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
    uint32_t writers[4] = {};  // funcoes SH-4 que escreveram (as 4 primeiras; BS_FN_ENTER)
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
    // N2b (oraculo de audio): os mais escritos e quem escreve
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.writes > b.second.writes; });
    std::fprintf(stderr, "[aica] mais escritos:\n");
    for (size_t i = 0; i < v.size() && i < 40 && v[i].second.writes; ++i) {
        const auto& [addr, s] = v[i];
        std::fprintf(stderr, "[aica]   %08X escritas %llu (mudou %u vezes) valor %08X -> %08X funcoes %08X %08X %08X %08X\n",
                     addr, (unsigned long long)s.writes, s.changes, s.firstValue, s.lastValue, s.writers[0],
                     s.writers[1], s.writers[2], s.writers[3]);
    }
    // por faixa de 64 KB
    std::map<uint32_t, std::pair<uint64_t, uint64_t>> pages;  // faixa -> (enderecos escritos, escritas)
    for (const auto& [addr, s] : v)
        if (s.writes) ++pages[addr >> 16].first, pages[addr >> 16].second += s.writes;
    for (const auto& [pg, n] : pages)
        std::fprintf(stderr, "[aica]   faixa %08X: %llu enderecos, %llu escritas\n", pg << 16,
                     (unsigned long long)n.first, (unsigned long long)n.second);
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
    if (write)
        for (uint32_t& w : s.writers)
            if (w == g_lastFn || !w) {
                w = g_lastFn;
                break;
            }
    if (!write)
        for (uint32_t& r : s.readers)
            if (r == g_pr || !r) {
                r = g_pr;
                break;
            }
    s.lastValue = v, s.lastCycle = cycles;
}
// Diagnostico (N2b-2): BS_AICA_LOG=<arquivo> grava, a cada 4 096 escritas da CPU na AICA (RAM de
// som e registradores de audio), o numero de escritas, o hash cumulativo (endereco, valor,
// tamanho) e o VBlank. Nao depende da linha do tempo: compara a sequencia de comandos ao audio
// entre modos de tempo em que o mesmo trabalho cai em VBlanks diferentes. Desligado por padrao.
std::FILE* g_audioLog = nullptr;
uint64_t   g_audioWrites = 0, g_audioHash = 1469598103934665603ull;
void audioLogFlush() {
    if (!g_audioLog) return;
    std::fprintf(g_audioLog, "%llu\t%016llX\tfim\n", (unsigned long long)g_audioWrites, (unsigned long long)g_audioHash);
    std::fclose(g_audioLog);
    g_audioLog = nullptr;
}
void audioLog(uint32_t p, uint32_t v, unsigned size) {
    static const bool on = [] {
        const char* e = std::getenv("BS_AICA_LOG");
        if (e && *e && (g_audioLog = std::fopen(e, "w"))) {
            std::fprintf(g_audioLog, "escritas\thash\tvblank\n");
            std::atexit(audioLogFlush);
        }
        return g_audioLog != nullptr;
    }();
    if (!on) return;
    // BS_AICA_LOG_TUDO=1: uma linha por escrita (endereco, valor, tamanho, VBlank), para separar
    // os escritores (driver, streams) quando o hash cumulativo diverge
    static const bool all = std::getenv("BS_AICA_LOG_TUDO") != nullptr;
    if (all)
        std::fprintf(g_audioLog, "w\t%08X\t%08X\t%u\t%llu\n", p, v, size, (unsigned long long)holly_vblank_count());
    for (uint32_t x : {p, v, uint32_t(size)}) g_audioHash = (g_audioHash ^ x) * 1099511628211ull;
    if (++g_audioWrites % 4096 == 0)
        std::fprintf(g_audioLog, "%llu\t%016llX\t%llu\n", (unsigned long long)g_audioWrites,
                     (unsigned long long)g_audioHash, (unsigned long long)holly_vblank_count());
}
} // namespace

bool aica_reg_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (inRam(p)) {
        v = 0;
        std::memcpy(&v, &g_ram[p & (kAicaRamSize - 1)], size);
        trace(false, p, v, time_now(c), c->pr);
        if (g_clock_log) clock_log_read(ClockRead::AudRd, p), clock_log_read(ClockRead::AudRd, v);
        return true;
    }
    if (p == kCtrl && size == 4) v = g_ctrl;
    else if (p == kRtcHi && size == 4) v = rtcNow(time_now(c)) >> 16;
    else if (p == kRtcLo && size == 4) v = rtcNow(time_now(c)) & 0xFFFF;
    else if (p == kRtcEn && size == 4) v = 0;
    else return false;
    if (g_clock_log && (p == kRtcHi || p == kRtcLo)) clock_log_read(ClockRead::Rtc, v);
    if (g_intc_trace && (p == kRtcHi || p == kRtcLo)) intc_trace_clock(IntcSource::Rtc, v);
    return true;
}

bool aica_reg_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (inRam(p)) {
        // Negativos do oraculo de audio (so diagnostico), BS_AICA_MUTACAO:
        //  <N>                a N-esima escrita da CPU na RAM de som se perde (e some do log: testa
        //                     o comparador);
        //  registrada:<hex>   a primeira escrita nesse endereco entra no log mas nao na RAM: so as
        //                     leituras seguintes da CPU podem acusar (testa a sensibilidade).
        static const char*    mut    = std::getenv("BS_AICA_MUTACAO");
        static const bool     logged = mut && std::strncmp(mut, "registrada:", 11) == 0;
        static const uint64_t lose   = mut && !logged ? std::strtoull(mut, nullptr, 10) : 0;
        static uint32_t       target = logged ? uint32_t(std::strtoul(mut + 11, nullptr, 16)) & ~3u : 0;
        static uint64_t       writes = 0;
        if (lose && ++writes == lose) return true;
        const bool skip = logged && target && (p & ~3u) == target;
        if (skip) target = 0;  // so a primeira
        if (!skip) std::memcpy(&g_ram[p & (kAicaRamSize - 1)], &v, size);
        audioLog(p, v, size);
        trace(true, p, v, time_now(c), c->pr);
        if (g_clock_log) clock_log_read(ClockRead::Aud, p), clock_log_read(ClockRead::Aud, v);
        return true;
    }
    // So registradores da AICA (0x00700000-0x0071FFFF) entram no artefato de audio: as escritas
    // do P4 (INTC, TMU, store queues) passam por aqui antes dos seus handlers e nao sao audio
    // (correcao 2026-10-16: antes eram 99,8% do AUD do N2b-0)
    if (p < 0x00700000 || p >= 0x00720000) return false;
    if (size != 4) return false;
    if (g_clock_log) clock_log_read(ClockRead::Aud, p), clock_log_read(ClockRead::Aud, v);
    audioLog(p, v, size);
    trace(true, p, v, time_now(c), c->pr);  // diagnostico: registradores tambem (BS_AICA_TRACE)
    if (p == kCtrl) {
        if (std::getenv("BS_AICA_TRACE"))  // diagnostico: reset/liberacao do ARM7 (bit 0)
            std::fprintf(stderr, "[aica] controle 00702C00 = %08X (ARM7 %s; ciclo %llu, PR %08X, funcao %08X)\n", v,
                         (v & 1) ? "em reset" : "liberado", (unsigned long long)time_now(c), c->pr, g_lastFn);
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
            const uint32_t now = rtcNow(time_now(c));
            const uint32_t val = p == kRtcHi ? (now & 0xFFFF) | ((v & 0xFFFF) << 16) : (now & 0xFFFF0000) | (v & 0xFFFF);
            aica_rtc_set(val, time_now(c));
            if (p == kRtcHi) g_rtcEnable = false;
        }
    } else {
        return false;
    }
    return true;
}

} // namespace bs
