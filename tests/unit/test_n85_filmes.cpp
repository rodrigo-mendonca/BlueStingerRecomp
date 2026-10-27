// N8.5: filmes (docs/N8-5-FILMES.md). No produto o conversor YUV do TA (so o filme legado, Sofdec, o usa) e um componente
// removido: escrever TA_YUV_TEX_BASE/CTRL, ler TA_YUV_TEX_CNT ou mandar macrobloco pela FIFO YUV e erro explicito. Na
// validacao o modelo continua (ta_yuv_converter_macroblocks_to_vram, test_m4_hw).
#include "test.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "pvr/pvr_regs.h"
#include "pvr/ta.h"

#include <stdexcept>
#include <string>
#include <vector>

using namespace bs;

#if !BS_VALIDACAO
namespace {
[[noreturn]] void throwingFatal(Sh4Context*, const char* what, uint32_t, uint32_t) { throw std::runtime_error(what); }
template <class F> std::string expectFatal(F&& fn) {
    sh4_set_fatal_handler(throwingFatal);
    std::string msg;
    try {
        fn();
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    sh4_set_fatal_handler(nullptr);
    return msg;
}
} // namespace

BS_TEST(n85_product_yuv_converter_is_removed) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    ta_reset();
    const uint32_t mb[8] = {};
    std::string    m[4];
    m[0] = expectFatal([&] { sh4_write32(&c, 0xA05F8148, 0x00200000); });  // TA_YUV_TEX_BASE
    m[1] = expectFatal([&] { sh4_write32(&c, 0xA05F814C, 0x00000001); });  // TA_YUV_TEX_CTRL
    m[2] = expectFatal([&] { (void)sh4_read32(&c, 0xA05F8150); });         // TA_YUV_TEX_CNT
    m[3] = expectFatal([&] { ta_fifo_write(&c, 0x10800000, mb); });        // macrobloco
    for (const auto& s : m) BS_CHECK(s.find("removidos do produto no N8.5") != std::string::npos);
    BS_CHECK(ta_fifo_write(&c, 0x10000000, mb));  // a FIFO de poligonos continua (N8.7)
}

// O boot carrega o estado do PVR medido (pvr_regs_load) direto, sem passar pela escrita do guest: valores nos registradores
// YUV nao disparam a guarda (revisao Codex); o guest que os usar depois continua recusado.
BS_TEST(n85_product_boot_pvr_state_does_not_trip_yuv_guard) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    pvr_regs_reset();
    ta_reset();
    std::vector<uint32_t> pvr(2048, 0);
    pvr[0x148 / 4] = 0x00200000;  // TA_YUV_TEX_BASE
    pvr[0x14C / 4] = 0x00000001;  // TA_YUV_TEX_CTRL
    bool ok = true;
    sh4_set_fatal_handler(throwingFatal);
    try {
        pvr_regs_load(pvr.data());
    } catch (const std::runtime_error&) {
        ok = false;
    }
    sh4_set_fatal_handler(nullptr);
    BS_CHECK(ok);
    BS_CHECK(expectFatal([&] { (void)sh4_read32(&c, 0xA05F8150); }).find("removidos do produto no N8.5") != std::string::npos);
    pvr_regs_reset();
}
#endif
