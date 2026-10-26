#include "cri_idle.h"

#include "cpu/sh4_intc.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_threads.h"
#include "system/syscall_service.h"
#include "memory/effect_counters.h"
#include "memory/sh4_tmu.h"
#include "memory/tmu_logical.h"
#include "pvr/holly_intc.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bs::transitional::cri {
namespace {

// Exclusoes do criterio validado (runtime/debug/cri_diag.cpp, modo efeito): medidores do TMU do
// CRI (B11) e o indice do rodizio do gerenciador de streams (+36 de 0x8C2DA958), que anda em toda
// chamada e so decide com empate.
constexpr uint32_t kMeterLo = 0x8C1DD1C8 & kRamMask, kMeterHi = 0x8C1DD23C & kRamMask;
constexpr uint32_t kRotor   = (0x8C2DA958 + 36) & kRamMask;
constexpr uint32_t kDeadStack = 0x4000;  // pilha morta da thread 3: [r15 - 16 KB, r15)

uint64_t epoch() {
    // mesma epoca de eventos do diagnostico (cri_diag.cpp), sem colisao: comparada campo a campo
    // pelo hash de uma tupla de contadores monotonicos
    uint64_t h = 1469598103934665603ull;
    for (uint64_t v : {sh4_irq_taken_count(), sh4_thread_switch_count(), holly_vblank_count(), holly_event_count(),
                       sys_disc_event_count()})
        h = (h ^ v) * 1099511628211ull;
    return h;
}

} // namespace

#ifdef _WIN32
uint8_t* alloc_watched_ram(size_t bytes) {
    void* p = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
    if (!p)
        throw std::runtime_error("tempo experimental: o sistema nao ofereceu memoria com rastreamento de escrita "
                                 "(MEM_WRITE_WATCH); use --tempo nativo");
    return static_cast<uint8_t*>(p);
}
void free_watched_ram(uint8_t* ram) {
    if (ram) VirtualFree(ram, 0, MEM_RELEASE);
}
#else
uint8_t* alloc_watched_ram(size_t) {
    throw std::runtime_error("tempo experimental: rastreamento de escrita so no Windows; use --tempo nativo");
}
void free_watched_ram(uint8_t*) {}
#endif

IdleDetector::IdleDetector(uint8_t* ram, size_t bytes, bool ignoreRam)
    : ram_(ram), bytes_(bytes), ignoreRam_(ignoreRam), shadow_(bytes), pages_(bytes / 4096 + 1) {}

// Compara as paginas escritas desde a ultima visita com a copia (palavra a palavra, fora das
// exclusoes) e atualiza a copia. Devolve true se algum valor mudou.
bool IdleDetector::ramChanged(uint32_t spLo, uint32_t spHi) {
#ifdef _WIN32
    ULONG_PTR count = pages_.size();
    ULONG     gran  = 0;
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, ram_, bytes_, pages_.data(), &count, &gran) != 0)
        throw std::runtime_error("tempo experimental: GetWriteWatch falhou");
    bool changed = false;
    for (ULONG_PTR i = 0; i < count; ++i) {
        const size_t off = static_cast<uint8_t*>(pages_[i]) - ram_;
        const size_t len = std::min<size_t>(gran, bytes_ - off);
        ++stats_.dirtyPages;
        if (!changed && std::memcmp(ram_ + off, shadow_.data() + off, len) != 0) {
            for (size_t w = off; w < off + len; w += 4) {
                const uint32_t a = uint32_t(w);
                if ((a >= spLo && a < spHi) || (a >= kMeterLo && a < kMeterHi) || a == kRotor) continue;
                if (std::memcmp(ram_ + w, shadow_.data() + w, 4) != 0) {
                    changed = true;
                    break;
                }
            }
        }
        std::memcpy(shadow_.data() + off, ram_ + off, len);
    }
    return changed;
#else
    (void)spLo, (void)spHi;
    return true;
#endif
}

bool IdleDetector::idle(const Sh4Context* c) {
    ++stats_.visits;
    const uint64_t e = epoch(), gd = sys_disc_state_hash(), tmu = sh4_tmu_state_hash() * 31 + tmu_logical_state_hash();  // N8.1: o TMU logico tambem
    const EffectCounters fx = g_effects;
    if (!primed_) {
        std::memcpy(shadow_.data(), ram_, bytes_);
#ifdef _WIN32
        ResetWriteWatch(ram_, bytes_);
#endif
        primed_ = true;
        epoch_ = e, hw_ = fx.hwWrites, sq_ = fx.sqFlushes, host_ = fx.hostWrites, gd_ = gd, tmu_ = tmu;
        return false;
    }
    const uint32_t spHi = c->r[15] & kRamMask, spLo = (c->r[15] - kDeadStack) & kRamMask;
    // a RAM e sempre comparada e a copia sempre atualizada (paginas escritas por outras threads
    // entre duas visitas tambem entram na copia)
    const bool ram   = ramChanged(spLo, spHi) && !ignoreRam_;
    const bool event = e != epoch_;
    const bool other = fx.hwWrites != hw_ || fx.sqFlushes != sq_ || fx.hostWrites != host_ || gd != gd_ || tmu != tmu_;
    epoch_ = e, hw_ = fx.hwWrites, sq_ = fx.sqFlushes, host_ = fx.hostWrites, gd_ = gd, tmu_ = tmu;
    if (event) ++stats_.eventDuring;
    else if (ram) ++stats_.effectRam;
    else if (other) ++stats_.effectOther;
    const bool idle = !event && !ram && !other;
    stats_.idle += idle;
    return idle;
}

} // namespace bs::transitional::cri
