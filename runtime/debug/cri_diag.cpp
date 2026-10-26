#include "debug/cri_diag.h"

#include "aica/aica_regs.h"
#include "cpu/sh4_intc.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "cpu/sh4_threads.h"
#include "debug/event_trace.h"
#include "cpu/write_watch.h"
#include "pvr/vram.h"
#include "system/syscall_service.h"
#include "memory/sh4_tmu.h"
#include "mods/mod_api.h"
#include "pvr/holly_intc.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace bs {
namespace {

constexpr uint32_t kPass      = 0x8C1586B8;  // passada do servidor do CRI
constexpr uint32_t kTable     = 0x8C2D0D84;  // 32 entradas de 120 bytes; +4 = ativa
constexpr uint32_t kEntries   = 32;
constexpr uint32_t kEntrySize = 120;
// Servidor ADX (8C178874): estrutura em 0x8C2F4F38, quantidade de handles em +0xB4, base da
// tabela em +0xB8, passo 0x3EF0; um handle trabalha com o estado (+64) entre 1 e 5.
constexpr uint32_t kAdx       = 0x8C2F4F38;
constexpr uint32_t kAdxStride = 0x3EF0;
// Servidor por objeto (8C175976): ponteiro para o objeto registrado (0 = nenhum).
constexpr uint32_t kSrvObj    = 0x8C1DD618;
// Gerenciador de streaming do objeto servidor (8C174888), disassembly 2026-10-16:
//  controle em 0x8C2DA958: +0 quantidade, +4 base, +8 leitura em curso (8C174C7C a acompanha),
//  +12/+20 tamanho e progresso do pedido (8C174E16), +36 indice do rodizio (8C174A36);
//  entradas de 68 bytes: +0 em uso, +4 estado (2 = pode ler), +12 limiar, +48 "buffer cheio"
//  (posto por 8C174888 quando nao ha espaco; limpo por 8C174998 quando o predicado 8C1749E0
//  ve o nivel do buffer do handle +60 no limiar ou abaixo), +60 handle.
constexpr uint32_t kStreamCtl    = 0x8C2DA958;
constexpr uint32_t kStreamStride = 68;
constexpr uint32_t kMgr      = 0x8C174888;  // metodo do objeto servidor (gerenciador de streaming)
constexpr uint32_t kIssue    = 0x8C174BAA;  // emite (ou continua) a leitura de um bloco
constexpr uint32_t kDrained  = 0x8C1749E0;  // predicado: buffer cheio voltou ao limiar
constexpr uint32_t kCandidate = 0x8C174A36; // proxima entrada que pode ler (rodizio)
constexpr uint32_t kLevel     = 0x8C19135A; // nivel do buffer do handle (r4), em *r5 (usado por 8C1749E0)

enum class Mode {
    Off, Count, SkipMw, SkipAll, SkipNoStream, SkipFirstPerVblank, RepeatNoStream,
    Flow, SkipAfterIdle, RepeatAfterIdle, TmuConsumer, Effect, SkipNoEffect, RepeatNoEffect, SkipNoEffectRamBlind,
    SkipNoEffectSqBlind
};

// B11: medidores do servidor do CRI (tempos lidos do TMU por 8C15D1A2 e gravados pela propria
// passada e pelo modulo 8C1584xx-8C158Bxx; disassembly 2026-10-16, revisao Codex). Ficam de fora
// os estados logicos da mesma area: 0x8C1DD1BC/1C0 (exclusao mutua da passada) e 0x8C1DD1C4
// (contador de VBlanks do callback 8C158A26, comparado com [0x8C1DD1B4] para pedir a troca).
// Os carimbos 0x8C1DD228/238 sao lidos por 8C158864 so para copia-los em 224/230; a decisao ali
// e pela bandeira logica 0x8C1DD23C (acesa pelo callback de VBlank).
constexpr uint32_t kMeterLo = 0x8C1DD1C8, kMeterHi = 0x8C1DD23C;  // 0x8C1DD23C: bandeira logica, fora
std::FILE*         g_meters = nullptr;
std::FILE*         g_flowLog = nullptr;  // BS_CRI_FLUXO_LOG: uma linha por passada com trabalho (VBlank, tipo)
uint64_t           g_meterVblank = ~0ull, g_meterChanges = 0, g_meterHash = 0, g_consumerHits = 0;

uint64_t meterHash(Sh4Context* c) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t a = kMeterLo; a < kMeterHi; a += 4) h = (h ^ sh4_read32(c, a)) * 1099511628211ull;
    return h;
}

// Uma linha por VBlank (primeira passada depois dele): os 31 medidores.
void logMeters(Sh4Context* c) {
    const uint64_t vb = holly_vblank_count();
    if (vb == g_meterVblank) return;
    g_meterVblank = vb;
    const uint64_t h = meterHash(c);
    g_meterChanges += h != g_meterHash, g_meterHash = h;
    if (!g_meters) return;
    std::fprintf(g_meters, "%llu", static_cast<unsigned long long>(vb));
    for (uint32_t a = kMeterLo; a < kMeterHi; a += 4) std::fprintf(g_meters, "\t%08X", sh4_read32(c, a));
    std::fputc('\n', g_meters);
}

struct State {
    Mode     mode = Mode::Off;
    uint64_t passes = 0, mwIdle = 0, adxIdle = 0, objNull = 0, allIdle = 0, noStream = 0, skipped = 0;
    uint64_t active[kEntries + 1] = {};
    uint64_t lastVblank = ~0ull;  // "menos1": VBlank da ultima passada pulada
    // fluxo / pular5 / repetir5
    uint32_t work = 0;  // trabalho da passada atual (involucros internos): 1 leitura em curso, 2 estado mudou, 4 emissao, 8 esvaziado, 16 candidata, 32 MW/ADX
    bool     lastIdle = false;    // a passada anterior foi ociosa...
    uint64_t lastIdleEpoch = 0;   // ...e terminou nesta epoca de eventos
    uint64_t lastIdleSwitches = 0;
    uint64_t worked = 0, idle = 0, eligible = 0, violations = 0, firstViolationVblank = 0, eventDuring = 0;
    uint64_t wakeSwitch = 0, wakeIrq = 0;  // trabalho depois de ociosa: o que mudou
    uint64_t kInflight = 0, kIssue = 0, kDrained = 0, kCand = 0, kChanged = 0, kMwAdx = 0, ties = 0;
    uint64_t streamPasses = 0, streamIdle = 0;  // passadas com stream em uso (gameplay)
    uint64_t curVblank = ~0ull, vbWorked = 0, maxWorkedPerVblank = 0, vblanksWithWork = 0, vblanks = 0;
    uint64_t firstStreamVblank = 0;
    uint64_t kPassChanged = 0;
    struct PassRec {
        uint64_t vb;
        uint32_t work;
        bool     newEpoch;
        uint32_t ctl8b, ctl8a, st0b, st0a, full0b, full0a;
    } ring[6] = {};
    uint64_t ringPos = 0;
    uint64_t levelCalls = 0, levelChanges = 0;  // nivel do buffer visto pelo predicado
    uint32_t lastLevel = 0, minLevel = ~0u, maxLevel = 0;
};
State g;

uint32_t activeEntries(Sh4Context* c) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < kEntries; ++i) n += sh4_read32(c, kTable + i * kEntrySize + 4) != 0;
    return n;
}

uint32_t activeAdx(Sh4Context* c) {
    const int32_t  count = int32_t(sh4_read32(c, kAdx + 0xB4));
    const uint32_t base  = sh4_read32(c, kAdx + 0xB8);
    uint32_t       n     = 0;
    for (int32_t i = 0; i < count && i < 64; ++i) {
        const uint32_t st = sh4_read32(c, base + uint32_t(i) * kAdxStride + 64);
        n += st >= 1 && st <= 5;
    }
    return n;
}

int32_t streamCount(Sh4Context* c) { return std::min<int32_t>(int32_t(sh4_read32(c, kStreamCtl)), 64); }
uint32_t streamEntry(Sh4Context* c, int32_t i) { return sh4_read32(c, kStreamCtl + 4) + uint32_t(i) * kStreamStride; }

uint32_t streamsInUse(Sh4Context* c) {
    uint32_t n = 0;
    for (int32_t i = 0; i < streamCount(c); ++i) n += sh4_read32(c, streamEntry(c, i)) != 0;
    return n;
}

// Estado logico do gerenciador (sem o indice do rodizio +36, que anda em toda chamada).
struct MgrSnap {
    uint32_t ctl[3];
    uint32_t e[16][5];
};
MgrSnap snap(Sh4Context* c) {
    MgrSnap s{};
    s.ctl[0] = sh4_read32(c, kStreamCtl + 8), s.ctl[1] = sh4_read32(c, kStreamCtl + 12);
    s.ctl[2] = sh4_read32(c, kStreamCtl + 20);
    for (int32_t i = 0; i < streamCount(c) && i < 16; ++i) {
        const uint32_t e = streamEntry(c, i);
        s.e[i][0] = sh4_read32(c, e), s.e[i][1] = sh4_read32(c, e + 4), s.e[i][2] = sh4_read32(c, e + 12);
        s.e[i][3] = sh4_read32(c, e + 48), s.e[i][4] = sh4_read32(c, e + 60);
    }
    return s;
}

// Epoca de eventos: muda com qualquer interrupcao aceita, troca de thread, VBlank-In ou evento do Holly
// entregue (inclusive com a interrupcao mascarada, como nas subpassadas do CRI).
uint64_t epoch() {
    return sh4_irq_taken_count() * 1000003ull + sh4_thread_switch_count() * 7919ull + holly_vblank_count() * 131ull +
           holly_event_count() * 17ull + sys_disc_event_count();
}

void wMgr(Sh4Context* c) {
    const bool    inflight = sh4_read32(c, kStreamCtl + 8) != 0;
    const MgrSnap before   = snap(c);
    mods::call_original(kMgr, c);
    if (inflight) ++g.kInflight, g.work |= 1;
    const MgrSnap after = snap(c);
    if (std::memcmp(&before, &after, sizeof before) != 0) ++g.kChanged, g.work |= 2;
}
void wIssue(Sh4Context* c) {
    ++g.kIssue, g.work |= 4;
    mods::call_original(kIssue, c);
}
void wDrained(Sh4Context* c) {
    mods::call_original(kDrained, c);
    if (c->r[0]) ++g.kDrained, g.work |= 8;
}
void wCandidate(Sh4Context* c) {
    uint32_t ready = 0;  // entradas que poderiam ler: em uso, sem "cheio", estado 2
    for (int32_t i = 0; i < streamCount(c); ++i) {
        const uint32_t e = streamEntry(c, i);
        ready += sh4_read32(c, e) != 0 && sh4_read32(c, e + 48) == 0 && sh4_read32(c, e + 4) == 2;
    }
    if (ready > 1) ++g.ties;  // so aqui o rodizio (+36) escolheria entre varias
    mods::call_original(kCandidate, c);
    if (c->r[0]) ++g.kCand, g.work |= 16;
}

void wLevel(Sh4Context* c) {
    const uint32_t out = c->r[5];
    mods::call_original(kLevel, c);
    const uint32_t lv = sh4_read32(c, out);
    if (g.levelCalls && lv != g.lastLevel) ++g.levelChanges;
    ++g.levelCalls, g.lastLevel = lv;
    g.minLevel = std::min(g.minLevel, lv), g.maxLevel = std::max(g.maxLevel, lv);
}

// Passada elegivel (anterior ociosa, nenhum evento desde entao) que trabalhou: o que fez e o
// estado do gerenciador depois dela.
void reportViolation(Sh4Context* c, uint64_t vb) {
    std::fprintf(stderr, "[cri] violacao V%llu: trabalho %02X; leitura em curso %u; streams:",
                 static_cast<unsigned long long>(vb), g.work, sh4_read32(c, kStreamCtl + 8));
    for (int32_t i = 0; i < streamCount(c); ++i) {
        const uint32_t e = streamEntry(c, i);
        if (sh4_read32(c, e))
            std::fprintf(stderr, " [%d estado %u cheio %u limiar %u]", i, sh4_read32(c, e + 4), sh4_read32(c, e + 48),
                         sh4_read32(c, e + 12));
    }
    std::fputc('\n', stderr);
    for (uint32_t k = 6; k >= 1; --k) {  // as ultimas passadas, da mais antiga para esta
        if (g.ringPos < k) continue;
        const State::PassRec& r = g.ring[(g.ringPos - k) % 6];
        std::fprintf(stderr,
                     "[cri]   V%llu trabalho %02X epoca %s; em curso %u->%u; entrada 0 estado %u->%u cheio %u->%u\n",
                     static_cast<unsigned long long>(r.vb), r.work, r.newEpoch ? "nova" : "mesma", r.ctl8b, r.ctl8a,
                     r.st0b, r.st0a, r.full0b, r.full0a);
    }
}

// BS_CRI_SNAP_VB=<V>: nos VBlanks V-2..V, compara TODA a RAM principal (16 MB) e a RAM de som
// (2 MB) antes e depois de cada passada (revisao Codex: duas janelas nao bastavam). Ficam de
// fora so:
//  - a pilha morta: [SP da entrada - 16 KB, SP da entrada), abaixo do topo depois do retorno
//    (quadros da passada e de tratadores de interrupcao que ja voltaram);
//  - o indice do rodizio (+36 do gerenciador), que anda em toda chamada e so decide com empate.
// Os medidores do TMU (kMeterLo..kMeterHi) contam a parte. MMIO e estado interno do runtime
// (fila de eventos, HLE do GD) nao entram: aparecem nos artefatos (CK, IST, CL).
constexpr size_t     kRamBytes = size_t(kRamMask) + 1;
std::vector<uint8_t> g_snapBefore;
uint64_t             g_snapVb = 0, g_snapReports = 0;

void snapRam(Sh4Context* c, std::vector<uint8_t>& out) {
    out.resize(kRamBytes + kAicaRamSize + kVramSize);  // RAM principal, de som e VRAM
    std::memcpy(out.data(), c->ram, kRamBytes);
    std::memcpy(out.data() + kRamBytes, aica_ram(), kAicaRamSize);
    std::memcpy(out.data() + kRamBytes + kAicaRamSize, vram_data(), kVramSize);
}

uint32_t g_snapSp = 0;
uint64_t g_snapPasses = 0, g_snapIdle = 0, g_snapIdleChanged = 0, g_snapIdleChangedNoEvent = 0, g_snapMetersOnly = 0;
// passadas com trabalho declarado (MW/ADX etc.) e sem evento: quantas nao mudaram nada fora dos medidores
uint64_t g_snapWorkNoEvent = 0, g_snapWorkNoEventUnchanged = 0;

void reportIdleChanges(Sh4Context* c, uint64_t vb, uint32_t work, bool eventDuring) {
    std::vector<uint8_t> now;
    snapRam(c, now);
    size_t   shown = 0, total = 0, meters = 0;
    char     line[2048];
    int      n = std::snprintf(line, sizeof line, "[cri] passada V%llu trabalho %02X%s mudou:", static_cast<unsigned long long>(vb),
                               work, eventDuring ? " (evento durante)" : "");
    const uint32_t spLo = (g_snapSp - 0x4000) & kRamMask, spHi = g_snapSp & kRamMask;
    for (size_t off = 0; off < now.size(); off += 4) {
        if (std::memcmp(&now[off], &g_snapBefore[off], 4) == 0) continue;
        const bool     sound = off >= kRamBytes;  // RAM de som ou VRAM
        const uint32_t a     = !sound ? 0x8C000000u | uint32_t(off)
                               : off < kRamBytes + kAicaRamSize ? 0x00800000u + uint32_t(off - kRamBytes)
                                                                 : 0xA5000000u + uint32_t(off - kRamBytes - kAicaRamSize);
        if (!sound && ((off >= spLo && off < spHi) || a == kStreamCtl + 36)) continue;
        if (!sound && a >= kMeterLo && a < kMeterHi) {
            ++meters;
            continue;
        }
        ++total;
        uint32_t o, w;
        std::memcpy(&o, &g_snapBefore[off], 4), std::memcpy(&w, &now[off], 4);
        if (shown++ < 24 && n < int(sizeof line) - 40) n += std::snprintf(line + n, sizeof line - n, " %08X:%08X->%08X", a, o, w);
    }
    ++g_snapPasses;
    if (!work) {
        ++g_snapIdle;
        g_snapIdleChanged += total != 0;
        g_snapIdleChangedNoEvent += total != 0 && !eventDuring;
        g_snapMetersOnly += !total && meters && !eventDuring;
    } else if (!eventDuring) {
        ++g_snapWorkNoEvent;
        g_snapWorkNoEventUnchanged += total == 0;
    }
    if (total && g_snapReports++ < 80) std::fprintf(stderr, "%s (%zu palavras)\n", line, total);
}

void flowPass(Sh4Context* c, uint32_t mw, uint32_t adx) {
    const uint64_t vb = holly_vblank_count();
    if (vb != g.curVblank) {
        if (g.curVblank != ~0ull) {
            ++g.vblanks;
            g.vblanksWithWork += g.vbWorked != 0;
            g.maxWorkedPerVblank = std::max(g.maxWorkedPerVblank, g.vbWorked);
        }
        g.curVblank = vb, g.vbWorked = 0;
    }
    const uint64_t e0       = epoch();
    const uint64_t sw0      = sh4_thread_switch_count();
    const bool     eligible = g.lastIdle && e0 == g.lastIdleEpoch;
    const bool     stream   = streamsInUse(c) != 0;
    if (stream && !g.firstStreamVblank) g.firstStreamVblank = vb;
    if (eligible) ++g.eligible;
    if (eligible && g.mode == Mode::SkipAfterIdle) {
        ++g.skipped;  // passada nao executada (so diagnostico); o estado nao muda
        return;
    }
    g.work = 0;
    if (mw || adx) ++g.kMwAdx, g.work |= 32;
    const MgrSnap before = snap(c);
    const bool    snapOn = g_snapVb && vb + 2 >= g_snapVb && vb <= g_snapVb;
    if (snapOn) snapRam(c, g_snapBefore), g_snapSp = c->r[15];
    if (eligible && g.mode == Mode::RepeatAfterIdle) {
        ++g.skipped;  // controle: executa duas vezes (so o tempo muda, se for ociosa)
        mods::call_original(kPass, c);
    }
    mods::call_original(kPass, c);
    const MgrSnap after = snap(c);
    // estado do gerenciador mudado em qualquer ponto da passada (tambem fora de 8C174888, p.ex.
    // por uma leitura concluida no servidor do gdFs)
    if (std::memcmp(&before, &after, sizeof before) != 0) ++g.kPassChanged, g.work |= 64;
    g.ring[g.ringPos++ % 6] = {vb, g.work, e0 != g.lastIdleEpoch, before.ctl[0], after.ctl[0], before.e[0][1],
                               after.e[0][1], before.e[0][3], after.e[0][3]};
    const bool worked = g.work != 0;
    if (snapOn) reportIdleChanges(c, vb, g.work, epoch() != e0);
    if (worked && g_flowLog) std::fprintf(g_flowLog, "%llu\t%02X\n", static_cast<unsigned long long>(vb), g.work);
    if (worked) {
        ++g.worked, ++g.vbWorked;
        if (eligible && epoch() != e0) {
            ++g.eventDuring;  // um evento (interrupcao, troca) aconteceu durante a passada: nao e violacao
        } else if (eligible) {
            if (!g.violations) g.firstViolationVblank = vb;
            if (g.violations < 20) reportViolation(c, vb);
            ++g.violations;
        } else if (g.lastIdle) {
            if (sw0 != g.lastIdleSwitches) ++g.wakeSwitch;
            else ++g.wakeIrq;
        }
    } else ++g.idle;
    if (stream) ++g.streamPasses, g.streamIdle += !worked;
    g.lastIdle      = !worked;
    g.lastIdleEpoch = epoch();
    g.lastIdleSwitches = sh4_thread_switch_count();
}

// --- Efeitos da passada (P1b; so no build de investigacao BS_WRITE_WATCH) ---
// Uma passada tem efeito se mudou algum valor na RAM principal (fora da pilha morta, dos medidores
// do TMU e do indice do rodizio), escreveu fora da RAM (MMIO, VRAM, store queues, AICA), o host
// escreveu na RAM do guest (provedores nativos) ou o estado do HLE do GD-ROM mudou. Escrita que
// regrava o mesmo valor nao e efeito (conta a parte: polling que escreve).
//  efeito   mede; pular6: nao executa a passada que vem depois de uma sem efeito, sem evento desde
//  entao (bloqueio ate o proximo evento, tambem com MW/ADX ativos); repetir6: controle (duas vezes);
//  pular6r: NEGATIVO, detector errado que ignora as mudancas na RAM.
struct Fx {
    uint64_t passes = 0, noEvent = 0, noEventEffect = 0, noEventIdle = 0, sameValueOnly = 0;
    uint64_t mwNoEvent = 0, mwNoEventIdle = 0, eligible = 0, violations = 0, eventDuring = 0;
    uint64_t kRam = 0, kHw = 0, kHost = 0, kGd = 0, kSq = 0, kTmu = 0;
    uint64_t firstViolationVb = 0, transientOnly = 0, sqOnly = 0;
};
Fx       g_fx;
bool     g_fxLastIdle  = false;
uint64_t g_fxLastEpoch = 0;

void effectPass(Sh4Context* c, uint32_t mw, uint32_t adx) {
    const uint64_t vb       = holly_vblank_count();
    const uint64_t e0       = epoch();
    const bool     eligible = g_fxLastIdle && e0 == g_fxLastEpoch;
    if (eligible) ++g_fx.eligible;
    if (eligible && (g.mode == Mode::SkipNoEffect || g.mode == Mode::SkipNoEffectRamBlind ||
                     g.mode == Mode::SkipNoEffectSqBlind)) {
        ++g.skipped;  // bloqueio simulado: a passada nao roda e nada muda
        // modo sombra: o monitor de progresso ve o bloqueio como a espera condicional que o N2b-2
        // declararia no back-edge do laco da thread 3 (so informa; a execucao nao muda)
        sh4_wait_point(c, 0x8C158860);
        return;
    }
    const uint64_t gd0 = sys_disc_state_hash(), tm0 = sh4_tmu_state_hash();
    g_ww               = WriteWatch{};
    g_ww.spLo = (c->r[15] - 0x4000) & kRamMask, g_ww.spHi = c->r[15] & kRamMask;
    g_ww.exLo[0] = kMeterLo & kRamMask, g_ww.exHi[0] = kMeterHi & kRamMask;
    g_ww.exLo[1] = (kStreamCtl + 36) & kRamMask, g_ww.exHi[1] = g_ww.exLo[1] + 4;
    g_ww.on = true;
    if (eligible && g.mode == Mode::RepeatNoEffect) {
        ++g.skipped;  // controle: so o tempo muda, se a passada e mesmo sem efeito
        mods::call_original(kPass, c);
    }
    mods::call_original(kPass, c);
    g_ww.on               = false;
    const WriteWatch& w   = g_ww;
    const uint64_t    net = ww_net_changes(c->ram);  // mudanca liquida na RAM (fim x inicio)
    const bool gdc      = sys_disc_state_hash() != gd0;
    const bool tmc      = sh4_tmu_state_hash() != tm0;  // p.ex. UNF aceso por leitura do TCNT
    const bool evDuring = epoch() != e0;
    bool       effect   = net || w.hwWrites || w.hostWrites || w.sqFlushes || gdc || tmc;
    if (g.mode == Mode::SkipNoEffectRamBlind) effect = w.hwWrites || w.hostWrites || w.sqFlushes || gdc || tmc;  // negativo
    if (g.mode == Mode::SkipNoEffectSqBlind) effect = net || w.hwWrites || w.hostWrites || gdc || tmc;  // negativo
    if (!evDuring && !net && w.ramChanges) ++g_fx.transientOnly;  // so mudancas desfeitas na propria passada
    // efeito so pela descarga da store queue (o negativo pular6s so e sensivel se isto ocorrer)
    g_fx.sqOnly += w.sqFlushes && !net && !w.hwWrites && !w.hostWrites && !gdc && !tmc;
    ++g_fx.passes;
    g_fx.kRam += net != 0, g_fx.kHw += w.hwWrites != 0, g_fx.kHost += w.hostWrites != 0, g_fx.kGd += gdc, g_fx.kSq += w.sqFlushes != 0, g_fx.kTmu += tmc;
    if (!evDuring) {
        ++g_fx.noEvent;
        g_fx.noEventEffect += effect, g_fx.noEventIdle += !effect;
        g_fx.sameValueOnly += !effect && w.ramWrites && !w.ramChanges;
        if (mw || adx) ++g_fx.mwNoEvent, g_fx.mwNoEventIdle += !effect;
    }
    if (eligible && effect) {
        if (evDuring) ++g_fx.eventDuring;
        else {
            if (!g_fx.violations) g_fx.firstViolationVb = vb;
            ++g_fx.violations;
        }
    }
    if (effect && g_flowLog)
        std::fprintf(g_flowLog, "%llu\t%c%c%c%c\n", static_cast<unsigned long long>(vb), net ? 'R' : '-',
                     w.hwWrites ? 'H' : '-', w.hostWrites ? 'O' : '-', gdc ? 'G' : '-');
    g_fxLastIdle  = !effect;
    g_fxLastEpoch = epoch();
}

void wrapper(Sh4Context* c) {
    const uint32_t mw   = activeEntries(c);
    const uint32_t adx  = activeAdx(c);
    const bool     obj  = sh4_read32(c, kSrvObj) != 0;
    const bool     idle = mw == 0 && adx == 0 && !obj;
    const bool     noSt = mw == 0 && adx == 0 && streamsInUse(c) == 0;
    logMeters(c);
    ++g.passes;
    ++g.active[mw];
    g.mwIdle += mw == 0;
    g.adxIdle += adx == 0;
    g.objNull += !obj;
    g.allIdle += idle;
    g.noStream += noSt;
    if (g.mode == Mode::Flow || g.mode == Mode::SkipAfterIdle || g.mode == Mode::RepeatAfterIdle) {
        flowPass(c, mw, adx);
        return;
    }
    if (g.mode == Mode::Effect || g.mode == Mode::SkipNoEffect || g.mode == Mode::RepeatNoEffect ||
        g.mode == Mode::SkipNoEffectRamBlind || g.mode == Mode::SkipNoEffectSqBlind) {
        effectPass(c, mw, adx);
        return;
    }
    if ((g.mode == Mode::SkipMw && mw == 0) || (g.mode == Mode::SkipAll && idle) ||
        (g.mode == Mode::SkipNoStream && noSt) ||
        (g.mode == Mode::SkipFirstPerVblank && g.lastVblank != holly_vblank_count())) {
        g.lastVblank = holly_vblank_count();
        ++g.skipped;  // passada nao executada (so diagnostico)
        return;
    }
    // B11, negativo: consumidor artificial que decide pelos medidores (logo, pelo TMU): com o
    // hash impar, gasta trabalho extra (ciclos estimados) antes da passada
    if (g.mode == Mode::TmuConsumer && (meterHash(c) & 1)) {
        ++g_consumerHits;
        c->cycles += 20000;
    }
    if (g.mode == Mode::RepeatNoStream && noSt) {
        ++g.skipped;  // controle: a mesma passada mais uma vez (so o tempo muda, se ela for ociosa)
        mods::call_original(kPass, c);
    }
    mods::call_original(kPass, c);
}

// BS_CRI_CESSAO_LOG=<arquivo>: trocas causadas pela exclusao mutua da passada (marcacao causal,
// segunda revisao Codex). A cessao explicita 8C17DDCC mascara as interrupcoes, poe o PR em SPC e
// salta ao despachante: a troca que ela pede e o PROXIMO evento do BS_EVENT_TRACE. Registra o
// numero de sequencia desse evento e o chamador (PR), so para os dois pontos da exclusao:
//  - 8C1588C2: a thread principal, em 8C1588AA, cede a vez enquanto [0x8C1DD1BC] = 1 (a thread 3
//    esta no meio de uma passada);
//  - 8C158854: a thread 3, no laco 8C158840, devolve a vez porque [0x8C1DD1C0] = 1 (exclusao
//    pedida).
// A cessao de cada render (8C158864, PR 8C15889C) e as trocas pedidas pelo callback de VBlank
// (8C17DFE8) NAO entram. A TN normalizada (TNC) remove so os eventos registrados aqui.
constexpr uint32_t kLock = 0x8C1588AA, kYield = 0x8C17DDCC;
constexpr uint32_t kLockYieldPr = 0x8C1588C2, kCriYieldPr = 0x8C158854;
std::FILE*         g_lockLog = nullptr;
uint64_t           g_lockCalls = 0, g_lockWithSwitch = 0, g_lockSwitches = 0;

void wLock(Sh4Context* c) {
    const uint64_t s0 = sh4_thread_switch_count();
    mods::call_original(kLock, c);
    const uint64_t s1 = sh4_thread_switch_count();
    ++g_lockCalls;
    if (s1 != s0) ++g_lockWithSwitch;
}

void wYield(Sh4Context* c) {
    const uint32_t pr = c->pr;
    const uint64_t e0 = event_trace_count();
    mods::call_original(kYield, c);  // volta quando esta thread for retomada
    if (pr != kLockYieldPr && pr != kCriYieldPr) return;
    ++g_lockSwitches;
    if (g_lockLog) std::fprintf(g_lockLog, "%llu\t%08X\n", static_cast<unsigned long long>(e0), pr);
}

void hook(uint32_t addr, Sh4Func fn) {
    if (!mods::has_slot(addr) || !mods::replace(addr, fn))
        throw std::runtime_error("BS_CRI_DIAG: funcao sem slot no build com mods");
}

} // namespace

void cri_diag_install() {
    const char* m = std::getenv("BS_CRI_MEDIDORES");
    const char* v = std::getenv("BS_CRI_DIAG");
    g             = State{};
    g_meterVblank = ~0ull, g_meterChanges = 0, g_meterHash = 0, g_consumerHits = 0;
    if (g_meters) std::fclose(g_meters), g_meters = nullptr;
    if (g_flowLog) std::fclose(g_flowLog), g_flowLog = nullptr;
    if (g_lockLog) std::fclose(g_lockLog), g_lockLog = nullptr;
    g_lockCalls = g_lockWithSwitch = g_lockSwitches = 0;
    g_snapReports = 0, g_snapPasses = g_snapIdle = g_snapIdleChanged = g_snapIdleChangedNoEvent = g_snapMetersOnly = 0;
    g_snapWorkNoEvent = g_snapWorkNoEventUnchanged = 0;  // sessao nova: nada herdado (revisao Codex)
    g_snapVb      = std::getenv("BS_CRI_SNAP_VB") ? std::strtoull(std::getenv("BS_CRI_SNAP_VB"), nullptr, 10) : 0;
    if (const char* f = std::getenv("BS_CRI_FLUXO_LOG"); f && *f)
        if (fopen_s(&g_flowLog, f, "w") != 0 || !g_flowLog)
            throw std::runtime_error(std::string("BS_CRI_FLUXO_LOG: nao foi possivel criar ") + f);
    if (m && *m) {
        if (fopen_s(&g_meters, m, "w") != 0 || !g_meters)
            throw std::runtime_error(std::string("BS_CRI_MEDIDORES: nao foi possivel criar ") + m);
        if (!v || !*v) v = "contar";  // os medidores sao lidos pela passada envolvida
    }
    if (const char* f = std::getenv("BS_CRI_CESSAO_LOG"); f && *f) {
        if (fopen_s(&g_lockLog, f, "w") != 0 || !g_lockLog)
            throw std::runtime_error(std::string("BS_CRI_CESSAO_LOG: nao foi possivel criar ") + f);
        if (!v || !*v) v = "contar";
    }
    if (!v || !*v) return;
    if (std::strcmp(v, "contar") == 0) g.mode = Mode::Count;
    else if (std::strcmp(v, "pular") == 0) g.mode = Mode::SkipMw;
    else if (std::strcmp(v, "pular2") == 0) g.mode = Mode::SkipAll;
    else if (std::strcmp(v, "pular3") == 0) g.mode = Mode::SkipNoStream;
    else if (std::strcmp(v, "menos1") == 0) g.mode = Mode::SkipFirstPerVblank;
    else if (std::strcmp(v, "repetir3") == 0) g.mode = Mode::RepeatNoStream;
    else if (std::strcmp(v, "fluxo") == 0) g.mode = Mode::Flow;
    else if (std::strcmp(v, "pular5") == 0) g.mode = Mode::SkipAfterIdle;
    else if (std::strcmp(v, "repetir5") == 0) g.mode = Mode::RepeatAfterIdle;
    else if (std::strcmp(v, "consumidor") == 0) g.mode = Mode::TmuConsumer;
    else if (std::strcmp(v, "efeito") == 0) g.mode = Mode::Effect;
    else if (std::strcmp(v, "pular6") == 0) g.mode = Mode::SkipNoEffect;
    else if (std::strcmp(v, "repetir6") == 0) g.mode = Mode::RepeatNoEffect;
    else if (std::strcmp(v, "pular6r") == 0) g.mode = Mode::SkipNoEffectRamBlind;
    else if (std::strcmp(v, "pular6s") == 0) g.mode = Mode::SkipNoEffectSqBlind;
    else
        throw std::runtime_error(std::string("BS_CRI_DIAG invalido: ") + v +
                                 " (contar, pular, pular2, pular3, menos1, repetir3, fluxo, pular5, repetir5, consumidor, efeito, pular6, repetir6, pular6r ou pular6s)");
    if ((g.mode == Mode::Effect || g.mode == Mode::SkipNoEffect || g.mode == Mode::RepeatNoEffect ||
         g.mode == Mode::SkipNoEffectRamBlind || g.mode == Mode::SkipNoEffectSqBlind) && !kWriteWatchBuilt)
        throw std::runtime_error("BS_CRI_DIAG=efeito/pular6/repetir6/pular6r precisa do build -DBS_WRITE_WATCH=ON");
    g_fx = Fx{}, g_fxLastIdle = false, g_fxLastEpoch = 0;
    if (!mods::has_slot(kPass))
        throw std::runtime_error("BS_CRI_DIAG precisa do build com slots (BS_MODS): a passada e chamada direto");
    if (!mods::replace(kPass, wrapper)) throw std::runtime_error("BS_CRI_DIAG: passada do CRI nao encontrada");
    if (g_lockLog) hook(kLock, wLock), hook(kYield, wYield);
    if (g.mode == Mode::Flow || g.mode == Mode::SkipAfterIdle || g.mode == Mode::RepeatAfterIdle) {
        hook(kMgr, wMgr), hook(kIssue, wIssue), hook(kDrained, wDrained), hook(kCandidate, wCandidate);
        if (!mods::replace(kLevel, wLevel)) throw std::runtime_error("BS_CRI_DIAG: 8C19135A nao encontrada");
    }
}

std::string cri_diag_summary() {
    if (g_meters) std::fflush(g_meters);
    if (g_flowLog) std::fflush(g_flowLog);
    if (g_lockLog) std::fflush(g_lockLog);
    if (g.mode == Mode::Off) return {};
    char b[1024];
    std::snprintf(b, sizeof b,
                  "[cri] passadas %llu; sem entrada ativa %llu, sem ADX ativo %llu, sem objeto servidor %llu, "
                  "tudo ocioso %llu, sem stream %llu; puladas %llu; por entradas ativas:",
                  static_cast<unsigned long long>(g.passes), static_cast<unsigned long long>(g.mwIdle),
                  static_cast<unsigned long long>(g.adxIdle), static_cast<unsigned long long>(g.objNull),
                  static_cast<unsigned long long>(g.allIdle), static_cast<unsigned long long>(g.noStream),
                  static_cast<unsigned long long>(g.skipped));
    std::string out = b;
    for (uint32_t n = 0; n <= kEntries; ++n)
        if (g.active[n]) {
            std::snprintf(b, sizeof b, " %u=%llu", n, static_cast<unsigned long long>(g.active[n]));
            out += b;
        }
    std::snprintf(b, sizeof b, "; medidores mudaram em %llu VBlanks; consumidor artificial %llu; exclusao 8C1588AA: %llu chamadas, %llu com troca; cessoes da exclusao %llu",
                  static_cast<unsigned long long>(g_meterChanges), static_cast<unsigned long long>(g_consumerHits),
                  static_cast<unsigned long long>(g_lockCalls), static_cast<unsigned long long>(g_lockWithSwitch),
                  static_cast<unsigned long long>(g_lockSwitches));
    out += b;
    if (g_snapVb) {
        std::snprintf(b, sizeof b, "; RAM (principal e de som) em V%llu-%llu: %llu passadas, %llu ociosas, %llu ociosas com RAM mudada (%llu sem evento durante a passada; %llu so nos medidores); com trabalho e sem evento %llu, delas sem mudanca %llu",
                      static_cast<unsigned long long>(g_snapVb - 2), static_cast<unsigned long long>(g_snapVb),
                      static_cast<unsigned long long>(g_snapPasses), static_cast<unsigned long long>(g_snapIdle),
                      static_cast<unsigned long long>(g_snapIdleChanged), static_cast<unsigned long long>(g_snapIdleChangedNoEvent),
                      static_cast<unsigned long long>(g_snapMetersOnly), static_cast<unsigned long long>(g_snapWorkNoEvent),
                      static_cast<unsigned long long>(g_snapWorkNoEventUnchanged));
        out += b;
    }
    out += "\n";
    if (g.mode == Mode::Flow || g.mode == Mode::SkipAfterIdle || g.mode == Mode::RepeatAfterIdle) {
        auto u = [](uint64_t v) { return static_cast<unsigned long long>(v); };
        std::snprintf(b, sizeof b,
                      "[cri] fluxo: com trabalho %llu, ociosas %llu; elegiveis (ociosa antes, mesma epoca) %llu, "
                      "violacoes %llu (primeira V%llu), com evento durante a passada %llu; acordadas por troca de thread %llu, por interrupcao %llu; "
                      "trabalho: leitura em curso %llu, emissoes %llu, buffers esvaziados %llu, candidatas %llu, "
                      "estado mudou %llu (na passada %llu), MW/ADX %llu; empates no rodizio %llu; com stream %llu (ociosas %llu, "
                      "primeiro stream V%llu); VBlanks %llu (com trabalho %llu, max %llu passadas com trabalho); "
                      "nivel do buffer: %llu consultas, %llu mudancas, %u-%u\n",
                      u(g.worked), u(g.idle), u(g.eligible), u(g.violations), u(g.firstViolationVblank), u(g.eventDuring),
                      u(g.wakeSwitch), u(g.wakeIrq), u(g.kInflight), u(g.kIssue), u(g.kDrained), u(g.kCand),
                      u(g.kChanged), u(g.kPassChanged), u(g.kMwAdx), u(g.ties), u(g.streamPasses), u(g.streamIdle),
                      u(g.firstStreamVblank), u(g.vblanks), u(g.vblanksWithWork), u(g.maxWorkedPerVblank),
                      u(g.levelCalls), u(g.levelChanges), g.levelCalls ? g.minLevel : 0u, g.maxLevel);
        out += b;
    }
    if (g.mode == Mode::Effect || g.mode == Mode::SkipNoEffect || g.mode == Mode::RepeatNoEffect ||
        g.mode == Mode::SkipNoEffectRamBlind || g.mode == Mode::SkipNoEffectSqBlind) {
        auto u = [](uint64_t v) { return static_cast<unsigned long long>(v); };
        std::snprintf(b, sizeof b,
                      "[cri] efeitos: passadas %llu; sem evento %llu (com efeito %llu, sem efeito %llu, das quais so "
                      "regravando o mesmo valor %llu, com mudanca so transitoria %llu); com MW/ADX e sem evento %llu (sem efeito %llu); efeito por tipo: "
                      "RAM %llu, MMIO/VRAM/SQ %llu, host %llu, GD-ROM %llu, descarga de SQ %llu (so ela %llu), TMU %llu; elegiveis %llu, violacoes %llu (primeira V%llu), "
                      "com evento durante %llu\n",
                      u(g_fx.passes), u(g_fx.noEvent), u(g_fx.noEventEffect), u(g_fx.noEventIdle), u(g_fx.sameValueOnly), u(g_fx.transientOnly),
                      u(g_fx.mwNoEvent), u(g_fx.mwNoEventIdle), u(g_fx.kRam), u(g_fx.kHw), u(g_fx.kHost), u(g_fx.kGd), u(g_fx.kSq), u(g_fx.sqOnly), u(g_fx.kTmu),
                      u(g_fx.eligible), u(g_fx.violations), u(g_fx.firstViolationVb), u(g_fx.eventDuring));
        out += b;
    }
    return out;
}

} // namespace bs
