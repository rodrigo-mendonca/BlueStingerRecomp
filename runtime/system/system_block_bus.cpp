// N8.7 (docs/N8-7-KAMUI.md, passo 5b): no produto, o system block do Holly (pvr/holly_sb.h) e o bloco nativo
// (native/render/kamui/system_block) depois do barramento sem dispositivos do N8.4 (o Maple, system/maple_bus): a
// revisao do barramento, o ultimo valor escrito (SB_FFST e a configuracao dos DMAs), os componentes removidos (drive de
// GD-ROM e DMA do G1 no N8.3, DMA do G2 no N8.6) como erro explicito e a partida da copia do canal 2 (4b), que acende
// o fim do DMA do PVR (ISTNRM bit 19) no banco de status (4a). O modelo do legado (pvr/holly_sb.cpp) fica na validacao.
#include "pvr/holly_sb.h"

#include "cpu/sh4_runtime.h"
#include "memory/sh4_dmac.h"
#include "pvr/holly_intc.h"
#include "render/kamui/system_block.h"
#include "system/maple_bus.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bs {
namespace {

using native::render::kamui::SystemBlock;

constexpr uint32_t kIstPvrDma = 1u << 19;

SystemBlock g_sb;
bool        g_c2dstWritten = false;  // BS_SB_MUTACAO=c2dst_ocupado

uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }

// N8.3 (docs/N8-3-BOOT.md): nao ha drive de GD-ROM nem DMA do G1 (o jogo le pelas syscalls, servico nativo)
bool removedRange(Sh4Context* c, uint32_t p, unsigned size, bool write, uint32_t v) {
    const SystemBlock::Region r = SystemBlock::removed(p, size, write, v);
    if (r == SystemBlock::Region::None) return false;
    static char msg[192];
    std::snprintf(msg, sizeof msg,
                  "%s de %u bytes em %08X (%s): componente removido do produto no N8.3 (sem drive de GD-ROM nem DMA "
                  "do G1; os dados vem das syscalls)",
                  write ? "escrita" : "leitura", size, p,
                  r == SystemBlock::Region::GdRom ? "registradores do GD-ROM" : "DMA do G1");
    std::fprintf(stderr, "[sb] %s\n", msg);
    sh4_fatal(c, msg, c->pc, v);
}

} // namespace

std::string holly_sb_summary() { return {}; }  // a comparacao com o modelo do legado so na validacao

void holly_sb_reset() {
    g_sb.reset();
    periph_reset();
}

bool holly_sb_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (removedRange(c, p, size, false, 0)) return true;
    if (!SystemBlock::contains(p, size)) return false;
    if (periph_read(c, a, v)) return true;
    // N2b-2, negativos (so testes): BS_SB_MUTACAO=c2dst_ocupado faz a primeira leitura de C2DST depois de cada escrita
    // dizer "ocupado" (o laco sincrono 8C1C1DC2 da uma volta); ffst_ocupado: a primeira leitura de SB_FFST (0x005F688C)
    // diz "FIFO ocupada" (bits 0 e 5; os lacos sincronos 8C096EE0/8C09724C dao uma volta)
    static const char* mut      = std::getenv("BS_SB_MUTACAO");
    static const bool  busyOnce = mut && std::strcmp(mut, "c2dst_ocupado") == 0;
    static bool        ffstBusy = mut && std::strcmp(mut, "ffst_ocupado") == 0;
    if (busyOnce && p == SystemBlock::kC2Dst && g_c2dstWritten) {
        g_c2dstWritten = false;
        v              = 1;
        return true;
    }
    if (ffstBusy && p == 0x005F688C) {
        ffstBusy = false;
        v        = 0x21;
        return true;
    }
    const SystemBlock::Read r = g_sb.read(p);
    v                         = r.value;
    if (r.first) std::fprintf(stderr, "[sb] leitura sem modelo: %08X -> %08X (ultimo valor escrito)\n", p, v);
    return true;
}

bool holly_sb_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (removedRange(c, p, size, true, v)) return true;
    if (!SystemBlock::contains(p, size)) return false;
    if (periph_write(c, a, v)) return true;
    const SystemBlock::Action act = g_sb.write(p, v);
    // N8.6 (docs/N8-6-AUDIO.md): sem AICA nem DMA do G2 no produto. A tabela do SDK so grava 0 nos registradores de
    // partida e o jogo so le o estado; uma partida pediria a transferencia para a AICA: erro
    if (act == SystemBlock::Action::G2Start) {
        static char msg[176];
        std::snprintf(msg, sizeof msg,
                      "partida do DMA do G2 em %08X: a AICA e o DMA do G2 foram removidos do produto no N8.6 (o som e "
                      "nativo)",
                      p);
        std::fprintf(stderr, "[sb] %s\n", msg);
        sh4_fatal(c, msg, c->pc, v);
    }
    if (p == SystemBlock::kC2Dst) g_c2dstWritten = true;
    // Diagnostico: BS_PVR_DEBUG=1 registra o disparo do DMA do canal 2 (SB_C2DST)
    static const bool debug = std::getenv("BS_PVR_DEBUG") != nullptr;
    static int        n     = 0;
    if (debug && p == SystemBlock::kC2Dst && n++ < 20)
        std::fprintf(stderr, "[sb] C2DST=%08X C2DSTAT=%08X C2DLEN=%08X\n", v, g_sb.reg(SystemBlock::kC2Dstat),
                     g_sb.reg(SystemBlock::kC2Dlen));
    // Diagnostico: BS_SB_LOG=1 registra as escritas nos registradores de DMA do G2 (0x005F7800-0x005F78FF)
    static const bool g2log = std::getenv("BS_SB_LOG") != nullptr;
    static int        g2n   = 0;
    if (g2log && p >= 0x005F7800 && p < 0x005F7900 && g2n++ < 400)
        std::fprintf(stderr, "[sb] G2 escrita %08X = %08X (ciclo %llu)\n", p, v, (unsigned long long)c->cycles);
    // a copia do canal 2 (4b) de SB_C2DLEN bytes para SB_C2DSTAT; no fim, ISTNRM bit 19 (fim do DMA do PVR) acende e a
    // interrupcao e avaliada no proximo poll (next_event = 0)
    if (act == SystemBlock::Action::Ch2Start && sh4_dmac_ch2_start(c, g_sb.at(SystemBlock::kC2Dstat),
                                                                   g_sb.at(SystemBlock::kC2Dlen), g_sb.at(SystemBlock::kC2Dst))) {
        holly_intc_raise(kIstPvrDma);
        c->next_event = 0;
    }
    return true;
}

} // namespace bs
