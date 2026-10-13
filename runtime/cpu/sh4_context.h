#pragma once
// Estado da CPU SH-4 sobre o qual o codigo gerado opera.
// Layout definitivo: marco M3 (ver ARCHITECTURE.md secao 5).
#include <cstdint>

namespace bs {

struct Sh4Context {
    alignas(16) float fr[2][16];  // [banco][reg]: FR/XF conforme FPSCR.FR
    uint32_t r[16];       // R0-R15 (banco ativo)
    uint32_t rbank[8];    // R0_BANK-R7_BANK inativos
    uint32_t sr;          // SR sem o bit T
    uint32_t t;           // bit T separado (0/1)
    uint32_t gbr, vbr, ssr, spc, sgr, dbr;
    uint32_t pr;
    uint32_t mach, macl;
    uint32_t fpscr;
    uint32_t fpul;
    uint32_t pc;          // apenas diagnostico
    uint64_t cycles;      // contador estimado para o scheduler
};

void sh4_reset(Sh4Context& c, uint32_t entry);

} // namespace bs
