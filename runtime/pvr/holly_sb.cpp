// System block do Holly (documentacao do KallistiOS, referencia de comportamento).
//   SB_SBREV 0x005F689C: revisao do barramento, 0x0000000B (so leitura; valor lido
//                        pelo boot do jogo em 8C1C41DC no Flycast)
// O controlador de interrupcoes (0x005F6900-0x005F693F) fica em holly_intc.cpp e e
// consultado antes deste arquivo.
//
// Registrador sem modelo proprio devolve o ultimo valor escrito. A primeira leitura
// de cada um e registrada: um registrador de status que precise de comportamento
// real (DMA em andamento, Maple) aparece aqui antes de virar divergencia.
#include "pvr/holly_sb.h"

#include "cpu/sh4_runtime.h"
#include "system/maple_bus.h"
#include "memory/sh4_dmac.h"
#include "pvr/holly_intc.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bs {
namespace {

constexpr uint32_t kBase  = 0x005F6800;
constexpr uint32_t kSize  = 0x1800;
constexpr uint32_t kSbRev = 0x005F689C;
constexpr uint32_t kC2Dstat = 0x005F6800, kC2Dlen = 0x005F6804, kC2Dst = 0x005F6808;
constexpr uint32_t kIstPvrDma = 1u << 19;

std::array<uint32_t, kSize / 4> g_regs{};
std::array<bool, kSize / 4>     g_reported{};
bool                            g_c2dstWritten = false;  // BS_SB_MUTACAO=c2dst_ocupado

uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }

// N8.3 (docs/N8-3-BOOT.md): nao ha drive de GD-ROM nem DMA do G1 (o jogo le pelas syscalls, servico nativo). Os
// registradores do drive (0x005F7000-0x005F70FF) nunca sao acessados (sonda do N0 e as referencias); do G1
// (0x005F7400-0x005F74FF), so a tabela de inicializacao do SDK (8C08BD74, tabela 8C0B8FB4) escreve GDST = 0 (DMA
// parado), aceita como registrador inerte. Qualquer outro acesso e um componente removido: erro explicito.
constexpr uint32_t kGdBase = 0x005F7000, kGdEnd = 0x005F7100, kG1Base = 0x005F7400, kG1End = 0x005F7500;
constexpr uint32_t kG1Gdst = 0x005F7418;

bool removedRange(Sh4Context* c, uint32_t p, unsigned size, bool write, uint32_t v) {
    const bool gd = p >= kGdBase && p < kGdEnd, g1 = p >= kG1Base && p < kG1End;
    if (!gd && !g1) return false;
    if (g1 && write && size == 4 && p == kG1Gdst && v == 0) return false;  // DMA parado (tabela do SDK)
    static char msg[192];
    std::snprintf(msg, sizeof msg,
                  "%s de %u bytes em %08X (%s): componente removido do produto no N8.3 (sem drive de GD-ROM nem DMA "
                  "do G1; os dados vem das syscalls)",
                  write ? "escrita" : "leitura", size, p, gd ? "registradores do GD-ROM" : "DMA do G1");
    std::fprintf(stderr, "[sb] %s\n", msg);
    sh4_fatal(c, msg, c->pc, v);
}

} // namespace

void holly_sb_reset() {
    g_regs.fill(0);
    g_reported.fill(false);
    periph_reset();
}

bool holly_sb_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (removedRange(c, p, size, false, 0)) return true;
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    if (periph_read(c, a, v)) return true;
    if (p == kSbRev) {
        v = 0x0000000B;
        return true;
    }
    const uint32_t i = (p - kBase) / 4;
    v                = g_regs[i];
    // N2b-2, negativo (so testes): BS_SB_MUTACAO=c2dst_ocupado faz a primeira leitura de C2DST
    // depois de cada escrita dizer "ocupado". O laco sincrono 8C1C1DC2 da uma volta: no tempo
    // experimental isso tem de ser erro explicito (nenhum evento libera a espera).
    // BS_SB_MUTACAO=ffst_ocupado: a primeira leitura de SB_FFST (0x005F688C) diz "FIFO ocupada"
    // (bits 0 e 5): os lacos sincronos 8C096EE0/8C09724C dao uma volta.
    static const char* mut      = std::getenv("BS_SB_MUTACAO");
    static const bool  busyOnce = mut && std::strcmp(mut, "c2dst_ocupado") == 0;
    static bool        ffstBusy = mut && std::strcmp(mut, "ffst_ocupado") == 0;
    if (busyOnce && p == kC2Dst && g_c2dstWritten) {
        g_c2dstWritten = false;
        v              = 1;
        return true;
    }
    if (ffstBusy && p == 0x005F688C) {
        ffstBusy = false;
        v        = 0x21;
        return true;
    }
    if (!g_reported[i]) {
        g_reported[i] = true;
        std::fprintf(stderr, "[sb] leitura sem modelo: %08X -> %08X (ultimo valor escrito)\n", p, v);
    }
    return true;
}

bool holly_sb_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (removedRange(c, p, size, true, v)) return true;
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    if (periph_write(c, a, v)) return true;
#if !BS_VALIDACAO
    // N8.6 (docs/N8-6-AUDIO.md): sem AICA nem DMA do G2 no produto. A tabela do SDK so grava 0 nos registradores de
    // partida (0x005F7818/38/58/78) e o jogo so le o estado; uma partida pediria a transferencia para a AICA: erro.
    if ((p == 0x005F7818 || p == 0x005F7838 || p == 0x005F7858 || p == 0x005F7878) && (v & 1)) {
        static char msg[176];
        std::snprintf(msg, sizeof msg,
                      "partida do DMA do G2 em %08X: a AICA e o DMA do G2 foram removidos do produto no N8.6 (o som e "
                      "nativo)",
                      p);
        std::fprintf(stderr, "[sb] %s\n", msg);
        sh4_fatal(c, msg, c->pc, v);
    }
#endif
    if (p != kSbRev) g_regs[(p - kBase) / 4] = v;
    if (p == kC2Dst) g_c2dstWritten = true;
    // Diagnostico: BS_PVR_DEBUG=1 registra o disparo do DMA do canal 2 (SB_C2DST)
    static const bool debug = std::getenv("BS_PVR_DEBUG") != nullptr;
    static int        n     = 0;
    if (debug && p == 0x005F6808 && n++ < 20)
        std::fprintf(stderr, "[sb] C2DST=%08X C2DSTAT=%08X C2DLEN=%08X\n", v, g_regs[0], g_regs[1]);
    // Diagnostico: BS_SB_LOG=1 registra as escritas nos registradores de DMA do G2
    // (0x005F7800-0x005F78FF: AICA, ext1, ext2, dev), ainda sem modelo
    static const bool g2log = std::getenv("BS_SB_LOG") != nullptr;
    static int        g2n   = 0;
    if (g2log && p >= 0x005F7800 && p < 0x005F7900 && g2n++ < 400)
        std::fprintf(stderr, "[sb] G2 escrita %08X = %08X (ciclo %llu)\n", p, v, (unsigned long long)c->cycles);
    // SB_C2DST = 1: DMA do canal 2 (SH-4 DMAC) de SB_C2DLEN bytes para SB_C2DSTAT. No fim,
    // C2DSTAT avanca, C2DLEN e C2DST zeram e ISTNRM bit 19 (fim do DMA do PVR) acende;
    // a interrupcao e avaliada no proximo poll (next_event = 0).
    if (p == kC2Dst && (v & 1)) {
        uint32_t& dest = g_regs[(kC2Dstat - kBase) / 4];
        uint32_t& len  = g_regs[(kC2Dlen - kBase) / 4];
        if (sh4_dmac_ch2_transfer(c, dest, len)) {
            dest += len;
            len = 0;
            g_regs[(kC2Dst - kBase) / 4] = 0;
            holly_intc_raise(kIstPvrDma);
            c->next_event = 0;
        }
    }
    return true;
}

} // namespace bs
