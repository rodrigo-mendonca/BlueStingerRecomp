// Caminho lento de memoria (tudo fora da RAM principal). M3: so registra o
// acesso; M4 implementa o mapa de regioes (BIOS/flash HLE, registradores,
// VRAM, AICA, store queues).
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "aica/aica_regs.h"
#include "cpu/sh4_intc.h"
#include "debug/hw_probe.h"
#include "memory/sh4_ccn.h"
#include "memory/sh4_dmac.h"
#include "memory/sh4_port.h"
#include "memory/sh4_tmu.h"
#include "memory/effect_counters.h"
#include "memory/sh4_sq.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"
#include "pvr/pvr_regs.h"
#include "pvr/vram.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <set>
#include <string>
#include <utility>

namespace bs {

WriteWatch     g_ww;       // diagnostico N2b (cpu/write_watch.h)
EffectCounters g_effects;  // N2b-2 (memory/effect_counters.h)

// Diagnostico N2b (so chamado no build BS_WRITE_WATCH): voltas por PC de retomada e declaracao
// sombra das esperas temporais do boot.
void ww_poll_note(Sh4Context* c, uint32_t resume, bool callReturn) {
    struct Count {
        uint64_t loops = 0, returns = 0, stuck = 0;  // voltas, retornos de chamada, voltas sem progresso
    };
    struct State {  // o estado arquitetural que um laco muda sem escrever na memoria
        uint32_t r[16], rbank[8], t, sr, gbr, vbr, ssr, spc, sgr, dbr, pr, fr[16], xf[16], fpscr, fpul, mach, macl;
        bool     operator==(const State&) const = default;
    };
    struct Slot {  // laco ativo (g_ww.loops[i] tem a janela de pilha e a marca de escrita)
        uint32_t pc  = 0;
        uint64_t use = 0;
        State    s{};
    };
    static std::unordered_map<uint32_t, Count>* polls = nullptr;
    static Slot                                 slots[kWwLoops];
    static uint64_t                             tick = 0;
    static bool                                    declare = false;
    static const bool                              init    = [] {
        if (const char* p = std::getenv("BS_WW_VOLTAS"); p && *p) {
            polls    = new std::unordered_map<uint32_t, Count>();
            g_ww.track = true;
            static std::string path = p;
            std::atexit([] {
                if (std::FILE* f = std::fopen(path.c_str(), "w")) {
                    for (const auto& [pc, n] : *polls)
                        std::fprintf(f, "%08X\t%llu\t%llu\t%llu\n", pc, static_cast<unsigned long long>(n.loops),
                                     static_cast<unsigned long long>(n.returns), static_cast<unsigned long long>(n.stuck));
                    std::fclose(f);
                }
            });
        }
        declare = std::getenv("BS_WW_DECLARAR") != nullptr;
        return true;
    }();
    (void)init;
    if (polls && callReturn) ++(*polls)[resume].returns;
    if (polls && !callReturn) {
        Count& n = (*polls)[resume];
        ++n.loops;
        State s;
        std::memcpy(s.r, c->r, sizeof s.r);
        std::memcpy(s.rbank, c->rbank, sizeof s.rbank);
        std::memcpy(s.fr, c->fr, sizeof s.fr);
        std::memcpy(s.xf, c->xf, sizeof s.xf);
        s.t = c->t, s.sr = c->sr, s.gbr = c->gbr, s.vbr = c->vbr, s.ssr = c->ssr, s.spc = c->spc;
        s.sgr = c->sgr, s.dbr = c->dbr, s.pr = c->pr, s.fpscr = c->fpscr;
        s.fpul = c->fpul, s.mach = c->mach, s.macl = c->macl;
        int i = 0;  // o slot deste laco, ou o menos usado recentemente
        for (int k = 0; k < kWwLoops; ++k) {
            if (slots[k].pc == resume) { i = k; break; }
            if (slots[k].use < slots[i].use) i = k;
        }
        WwLoop& w = g_ww.loops[i];
        if (slots[i].pc == resume && !w.dirty && slots[i].s == s) ++n.stuck;
        slots[i].pc = resume, slots[i].use = ++tick, slots[i].s = s;
        w.hi    = c->r[15] & 0x00FFFFFFu;  // frames abaixo do r15 do laco: chamadas dentro da volta
        w.lo    = w.hi > 0x10000u ? w.hi - 0x10000u : 0;
        w.dirty = false;
    }
    if (declare && !callReturn &&
        (resume == 0x8C172624 || resume == 0x8C17262A || resume == 0x8C172804 || resume == 0x8C172EF2 ||
         resume == 0x8C1C6CB0 || resume == 0x8C1C6D08))
        sh4_wait_point(c, resume);  // modo sombra: so informa o monitor
}
namespace {

// Cada registrador distinto e registrado uma vez (area 0 e P4 por endereco; o resto,
// memorias como VRAM e FIFOs, por pagina de 64 KB), para enxergar todo o hardware
// que o boot toca sem inundar o log.
void unmapped(const char* kind, uint32_t a, uint32_t v) {
    static std::set<std::pair<std::string, uint32_t>> seen;
    const uint32_t phys = a & 0x1FFFFFFF;
    const bool     reg  = a >= 0xE0000000 || phys < 0x00800000;
    if (seen.size() >= 4096 || !seen.emplace(kind, reg ? a : (a & ~0xFFFFu)).second) return;
    std::fprintf(stderr, "[mem] %s nao mapeado: %08X (valor %08X)%s\n", kind, a, v, reg ? "" : " [pagina]");
}

// Registradores ja modelados, implementados conforme o boot os pede (M4).
bool hwRead(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    return vram_read(c, a, size, v) ||           // area 1
           pvr_reg_read(c, a, size, v) ||        // 0x005F8000
           holly_intc_read(c, a, size, v) ||     // 0x005F6900 (antes do system block)
           holly_sb_read(c, a, size, v) ||       // 0x005F6800
           aica_reg_read(c, a, size, v) ||       // 0x00702C00, RTC
           sh4_intc_read(a, size, v) ||          // P4: ICR/IPR, INTEVT
           sh4_port_read(a, size, v) ||          // P4: PCTRA/PDTRA
           sh4_tmu_read(c, a, size, v) ||        // P4: TMU
           sh4_sq_read(a, size, v) ||            // P4: store queues, QACR
           sh4_dmac_read(a, size, v) ||          // P4: DMAC
           sh4_ccn_read(a, size, v);             // P4: CCR, arrays de cache
}
bool hwWrite(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    ++g_effects.hwWrites;  // N2b-2: efeito fora da RAM (detector de ociosidade da thread 3)
#if BS_WRITE_WATCH
    ww_dirty_all();
    if (g_ww.on) ++g_ww.hwWrites;  // diagnostico N2b: efeito fora da RAM
#endif
    return vram_write(c, a, size, v) ||
           pvr_reg_write(c, a, size, v) ||
           holly_intc_write(c, a, size, v) ||
           holly_sb_write(c, a, size, v) ||
           aica_reg_write(c, a, size, v) ||
           sh4_intc_write(a, size, v) ||
           sh4_port_write(a, size, v) ||
           sh4_tmu_write(c, a, size, v) ||
           sh4_sq_write(a, size, v) ||
           sh4_dmac_write(a, size, v) ||
           sh4_ccn_write(a, size, v);
}

} // namespace

uint8_t sh4_read8_slow(Sh4Context* c, uint32_t a) {
    if (g_hw_probe) hw_probe_access(c, a, 1, ProbeKind::Read);
    if (uint32_t v; hwRead(c, a, 1, v)) return uint8_t(v);
    unmapped("read8", a, 0);
    return 0;
}
uint16_t sh4_read16_slow(Sh4Context* c, uint32_t a) {
    if (g_hw_probe) hw_probe_access(c, a, 2, ProbeKind::Read);
    if (a & 1) sh4_address_error(c, a, false);
    if (uint32_t v; hwRead(c, a, 2, v)) return uint16_t(v);
    unmapped("read16", a, 0);
    return 0;
}
uint32_t sh4_read32_slow(Sh4Context* c, uint32_t a) {
    if (g_hw_probe) hw_probe_access(c, a, 4, ProbeKind::Read);
    if (a & 3) sh4_address_error(c, a, false);
    if (uint32_t v; hwRead(c, a, 4, v)) return v;
    unmapped("read32", a, 0);
    return 0;
}
void sh4_write8_slow(Sh4Context* c, uint32_t a, uint8_t v) {
    if (g_hw_probe) hw_probe_access(c, a, 1, ProbeKind::Write);
    if (hwWrite(c, a, 1, v)) return;
    unmapped("write8", a, v);
}
void sh4_write16_slow(Sh4Context* c, uint32_t a, uint16_t v) {
    if (g_hw_probe) hw_probe_access(c, a, 2, ProbeKind::Write);
    if (a & 1) sh4_address_error(c, a, true);
    if (hwWrite(c, a, 2, v)) return;
    unmapped("write16", a, v);
}
void sh4_write32_slow(Sh4Context* c, uint32_t a, uint32_t v) {
    if (g_hw_probe) hw_probe_access(c, a, 4, ProbeKind::Write);
    if (a & 3) sh4_address_error(c, a, true);
    if (hwWrite(c, a, 4, v)) return;
    unmapped("write32", a, v);
}

} // namespace bs
