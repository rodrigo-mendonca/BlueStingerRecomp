#pragma once
// Estado da CPU SH-4 sobre o qual o codigo gerado opera (ARCHITECTURE.md secao 5).
#include <cstddef>
#include <cstdint>

namespace bs {

// Bits de SR (T fica separado em Sh4Context::t).
constexpr uint32_t kSrT     = 1u << 0;
constexpr uint32_t kSrS     = 1u << 1;
constexpr uint32_t kSrImask = 0xFu << 4;
constexpr uint32_t kSrQ     = 1u << 8;
constexpr uint32_t kSrM     = 1u << 9;
constexpr uint32_t kSrFD    = 1u << 15;
constexpr uint32_t kSrBL    = 1u << 28;
constexpr uint32_t kSrRB    = 1u << 29;
constexpr uint32_t kSrMD    = 1u << 30;
constexpr uint32_t kSrMask  = 0x700083F3u;  // bits gravaveis

// Bits de FPSCR.
constexpr uint32_t kFpscrRM   = 3u << 0;
constexpr uint32_t kFpscrDN   = 1u << 18;
constexpr uint32_t kFpscrPR   = 1u << 19;
constexpr uint32_t kFpscrSZ   = 1u << 20;
constexpr uint32_t kFpscrFR   = 1u << 21;
constexpr uint32_t kFpscrMask = 0x003FFFFFu;

struct Sh4Context {
    uint32_t r[16];      // R0-R15 do banco ativo
    uint32_t rbank[8];   // R0-R7 do banco inativo
    uint32_t sr;         // SR sem o bit T
    uint32_t t;          // bit T (0/1)
    uint32_t gbr, vbr, ssr, spc, sgr, dbr;
    uint32_t pr;
    uint32_t mach, macl;
    uint32_t fpscr;
    uint32_t fpul;
    uint32_t fr[16];     // banco ativo (FPSCR.FR), em bits
    uint32_t xf[16];     // banco inativo
    uint32_t pc;         // PC da ultima instrucao que precisou dele (diagnostico/excecoes)

    uint64_t cycles;      // contador estimado
    uint64_t next_event;  // quando sh4_service() deve rodar
    uint32_t ret_expect;  // endereco de retorno esperado pelo chamador (verificacao de rts)
    uint32_t ret_sp;      // R15 do chamador nesse ponto: identifica a thread do guest (sh4_threads)

    uint8_t* ram;         // 16 MB de RAM principal
    void*    owner;       // runtime dono deste contexto
};

void     sh4_reset(Sh4Context& c, uint32_t entry);
uint32_t sh4_get_sr(const Sh4Context& c);
void     sh4_set_sr(Sh4Context& c, uint32_t v);     // troca bancos se RB mudar
void     sh4_set_fpscr(Sh4Context& c, uint32_t v);  // troca FR/XF se FR mudar; ajusta arredondamento do host
void     sh4_apply_host_fp_mode(uint32_t fpscr);    // so o modo do host (RM, DN), sem trocar bancos

// Tabela de FSCA: 0x10000 pares (seno, cosseno), gerada uma vez com arredondamento
// ao mais proximo. Funcoes da libm nao podem ser usadas na thread da CPU porque o
// host roda no modo de arredondamento do guest (RM=01, para zero, no Dreamcast).
const float* sh4_fsca_table();

} // namespace bs
