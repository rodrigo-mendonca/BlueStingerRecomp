// N8.2 (D-N8-3): o produto (sem BS_VALIDACAO) nao tem as mutacoes nem as sondas exclusivas de teste. Estes stubs ocupam
// o lugar de debug/hw_probe.cpp, debug/cycle_noise.cpp e debug/cri_diag.cpp: sem efeito, e a variavel de ambiente de
// cada um e RECUSADA (erro e saida 2), para um teste nunca passar no vazio com o executavel errado. A instrumentacao das
// referencias (BS_EVENT_TRACE, BS_INTC_TRACE etc.) continua no produto.
#include "debug/cri_diag.h"
#include "debug/cycle_noise.h"
#include "debug/gfx_probe.h"
#include "debug/hw_probe.h"

#include <cstdio>
#include <cstdlib>

namespace bs {
namespace {

void refuse(const char* var, const char* what) {
    if (const char* v = std::getenv(var); v && *v) {
        std::fprintf(stderr, "erro: %s (%s) so existe no build de validacao (-DBS_VALIDACAO=ON, preset msvc-validacao); "
                             "este executavel e o do produto\n",
                     var, what);
        std::exit(2);
    }
}

const bool kChecked = [] {
    refuse("BS_HW_PROBE", "sonda de hardware do N0");
    refuse("BS_CICLOS_RUIDO", "mutacao de ruido de ciclos");
    refuse("BS_CRI_DIAG", "diagnostico do CRI");
    refuse("BS_HW_PROBE_GRAFICOS", "medicao da fronteira grafica do N8.7");
    return true;
}();

} // namespace

bool g_hw_probe = false;

void        hw_probe_access(const Sh4Context*, uint32_t, unsigned, ProbeKind) {}
void        hw_probe_dispatch(const Sh4Context*, uint32_t) {}
void        hw_probe_init(const std::string&) {}
bool        hw_probe_flush() { return false; }
std::string hw_probe_dump() { return {}; }

bool g_gfx_probe = false;
void gfx_probe_init(const std::string&) {}
bool gfx_probe_flush() { return false; }
void gfx_probe_ta(const Sh4Context*, uint32_t, GfxPath) {}
void gfx_probe_dma(const Sh4Context*, uint32_t, uint32_t, uint32_t) {}
void gfx_probe_reg(const Sh4Context*, uint32_t, bool, uint32_t) {}
void gfx_probe_vram_write(const Sh4Context*, uint32_t, unsigned) {}
void gfx_probe_start_render(const Sh4Context*) {}
void gfx_probe_list_init() {}
void gfx_probe_texture(uint32_t, uint32_t, uint32_t) {}

void cycle_noise(Sh4Context*) {}

void        cri_diag_install() {}
std::string cri_diag_summary() { return {}; }

} // namespace bs
