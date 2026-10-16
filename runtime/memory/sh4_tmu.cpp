// TMU do SH-4 (manual SH7750, cap. 12 "Timer Unit"):
//   TOCR 0xFFD80000 (8)   TSTR 0xFFD80004 (8, bits 2-0 = STR2..STR0)
//   canal n (n = 0, 1, 2), base 0xFFD80008 + 12 * n:
//     TCOR (32) constante de recarga, TCNT (32) contador, TCR (16):
//       bits 2-0 TPSC (0 P/4, 1 P/16, 2 P/64, 3 P/256, 4 P/1024; 5-7 RTC/externo),
//       bit 5 UNIE (interrupcao de underflow), bit 8 UNF (underflow, limpo escrevendo 0)
//   TCPR2 0xFFD8002C (captura do canal 2)
// TCNT decrementa a cada tick; ao passar de 0 recarrega TCOR e acende UNF.
// Relogio periferico P = 50 MHz com a CPU a 200 MHz: 4 ciclos da CPU por tick de P.
// Como o tempo e o contador estimado de ciclos, o valor lido e aproximado (o mesmo
// vale para tudo o que depende de tempo; ver STATUS.md).
// M4: o jogo usa o canal 0 como contador livre (TCOR0 = TCNT0 = 0xFFFFFFFF, P/64, sem
// interrupcao). Interrupcao de underflow, captura e fontes RTC/externas sao registradas e
// ainda nao modeladas.
#include "memory/sh4_tmu.h"
#include "pvr/holly_intc.h"
#include "cpu/time_hooks.h"
#include "debug/clock_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bs {
namespace {

constexpr uint32_t kTocr = 0xFFD80000, kTstr = 0xFFD80004, kCh0 = 0xFFD80008, kTcpr2 = 0xFFD8002C;
constexpr uint32_t kCpuPerPeriph = 4;

struct Channel {
    uint32_t tcor = 0xFFFFFFFF, tcr = 0;
    uint32_t base = 0xFFFFFFFF;  // TCNT no ciclo baseCycle
    uint64_t baseCycle = 0;
};

Channel  g_ch[3];
uint8_t  g_tocr = 0, g_tstr = 0;
uint32_t g_tcpr2 = 0;

uint64_t divider(const Channel& ch) {
    static const uint32_t kDiv[5] = {4, 16, 64, 256, 1024};
    const uint32_t        tpsc    = ch.tcr & 7;
    return uint64_t(kCpuPerPeriph) * (tpsc < 5 ? kDiv[tpsc] : 4);
}

// TCNT no ciclo `now` (acende UNF se houve underflow).
uint32_t count(int n, uint64_t now) {
    Channel& ch = g_ch[n];
    if (!(g_tstr & (1u << n)) || now <= ch.baseCycle) return ch.base;
    const uint64_t ticks = (now - ch.baseCycle) / divider(ch);
    if (ticks <= ch.base) return ch.base - uint32_t(ticks);
    ch.tcr |= 1u << 8;  // UNF
    const uint64_t period = uint64_t(ch.tcor) + 1;
    return ch.tcor - uint32_t((ticks - ch.base - 1) % period);
}

// Fixa o valor atual como nova base (antes de mudar TSTR, TCR ou TCNT).
void rebase(int n, uint64_t now) {
    g_ch[n].base      = count(n, now);
    g_ch[n].baseCycle = now;
}

bool inRange(uint32_t a) { return a >= kTocr && a <= kTcpr2 + 3; }

void warnTpsc(int n, uint32_t tcr) {
    static bool warned[3];
    if (((tcr & 7) >= 5 || (tcr & (1u << 5))) && !warned[n]) {
        warned[n] = true;
        std::fprintf(stderr, "[tmu] canal %d: TCR=%04X (fonte RTC/externa ou UNIE) ainda sem modelo\n", n, tcr);
    }
}

} // namespace

void sh4_tmu_reset() {
    for (auto& ch : g_ch) ch = Channel{};
    g_tocr = g_tstr = 0;
    g_tcpr2         = 0;
}

// Diagnostico N2b (D7): BS_TMU_DIAG=quadro faz o TCNT lido valer o do instante em que o ultimo
// VBlank-In foi entregue (ponto de poll; nao o inicio exato do campo), como se o tempo parasse
// dentro do passo. O avanco por ciclos continua: aproxima o modelo do N2b, nao o reproduz.
// Serve para medir se os consumidores do TMU mudam o comportamento do jogo. Desligado.
// Outras variantes (N2b-0): "agendado" usa o instante devido do ultimo VBlank-In (inicio logico
// do campo; so o provedor nativo sabe); "zero" para o TMU de vez (instante 0); "dobro" faz o TMU
// andar duas vezes mais rapido. Medem quanto os consumidores dependem do valor.
enum class TmuDiag { Off, Quadro, Agendado, Zero, Dobro };
TmuDiag tmuDiag() {
    static const TmuDiag d = [] {
        const char* v = std::getenv("BS_TMU_DIAG");
        if (!v || !*v) return TmuDiag::Off;
        if (std::strcmp(v, "quadro") == 0) return TmuDiag::Quadro;
        if (std::strcmp(v, "agendado") == 0) return TmuDiag::Agendado;
        if (std::strcmp(v, "zero") == 0) return TmuDiag::Zero;
        if (std::strcmp(v, "dobro") == 0) return TmuDiag::Dobro;
        std::fprintf(stderr, "BS_TMU_DIAG invalido: %s (quadro, agendado, zero ou dobro)\n", v);
        std::exit(2);
    }();
    return d;
}

uint64_t tmuNow(Sh4Context* c) {
    switch (tmuDiag()) {
    case TmuDiag::Quadro: return holly_last_vblank_instant();
    case TmuDiag::Agendado: {
        const uint64_t due = time_provider().last_vblank_due();
        return due ? due : holly_last_vblank_instant();
    }
    case TmuDiag::Zero: return 0;
    case TmuDiag::Dobro: return 2 * time_now(c);
    default: return time_now(c);
    }
}

bool sh4_tmu_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    if (!inRange(a)) return false;
    const uint64_t now = tmuNow(c);
    if (a == kTocr) v = g_tocr;
    else if (a == kTstr) v = g_tstr;
    else if (a == kTcpr2) v = g_tcpr2;
    else {
        const uint32_t off = a - kCh0, n = off / 12, r = off % 12;
        if (n > 2) return false;
        if (r == 0) v = g_ch[n].tcor;
        else if (r == 4) {
            v = count(int(n), now);
            if (g_clock_log) clock_log_read(ClockRead::Tmu, v);
        }
        else if (r == 8) {
            count(int(n), now);  // UNF atualizado
            v = g_ch[n].tcr;
        } else return false;
    }
    if (size == 1) v &= 0xFF;
    else if (size == 2) v &= 0xFFFF;
    return true;
}

bool sh4_tmu_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    (void)size;
    if (!inRange(a)) return false;
    const uint64_t now = time_now(c);
    if (a == kTocr) {
        g_tocr = uint8_t(v & 1);
    } else if (a == kTstr) {
        for (int n = 0; n < 3; ++n) rebase(n, now);
        g_tstr = uint8_t(v & 7);
    } else if (a == kTcpr2) {
        g_tcpr2 = v;
    } else {
        const uint32_t off = a - kCh0, n = off / 12, r = off % 12;
        if (n > 2) return false;
        Channel& ch = g_ch[n];
        if (r == 0) ch.tcor = v;
        else if (r == 4) {
            ch.base      = v;
            ch.baseCycle = now;
        } else if (r == 8) {
            rebase(int(n), now);
            // UNF/ICPF: so podem ser limpos (escrever 1 mantem o valor atual)
            const uint32_t flags = ch.tcr & (v | ~0x300u) & 0x300u;
            ch.tcr               = (v & ~0x300u & 0x3FF) | flags;
            warnTpsc(int(n), ch.tcr);
        } else return false;
    }
    return true;
}

} // namespace bs
