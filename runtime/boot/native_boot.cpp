// Boot nativo (ver native_boot.h). O estado foi medido na entrada do jogo no oraculo
// (snapshot 2026-10-14, BIOS HLE reios do Flycast) e conferido regiao por regiao:
//  - 1ST_READ.BIN em 0x8C010000: identico ao arquivo do disco;
//  - IP.BIN em 0x8C008000: identico, exceto a pilha do proprio IP.BIN (0x8C00F3B0+);
//  - 0x8C000000-0x8C007FFF: 0xFF, com variaveis do sistema, vetores e stubs das syscalls
//    e uma tabela em 0x8C003174 (bios_entry_state.inc);
//  - 0x8CDE0000-0x8CFFFFFF: restos da rotina de boot do IP.BIN (tabela de permutacao e
//    pedacos do executavel), NAO reproduzidos: o boot nativo deixa zeros. A regressao do
//    trace (STATUS.md, boot nativo) mostra se o jogo depende deles.
//  - registradores do PVR/Holly deixados pelo BIOS: bios_entry_state.inc.
#include "boot/native_boot.h"

#include "aica/aica_regs.h"
#include "cpu/irq_bridge.h"
#include "cpu/sh4_intc.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "disc/ip_bin.h"
#include "disc/iso9660.h"
#include "disc/scramble.h"
#include "hle/bios.h"
#include "gdrom/gdrom.h"
#include "known_versions.h"
#include "memory/sh4_ccn.h"
#include "memory/sh4_dmac.h"
#include "memory/sh4_port.h"
#include "memory/sh4_sq.h"
#include "memory/sh4_tmu.h"
#include "memory/sys_logical.h"
#include "memory/tmu_logical.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"
#include "pvr/pvr_regs.h"
#include "pvr/ta.h"
#include "pvr/vram.h"
#include "runtime_data.h"
#include "util/hash.h"

#include <cstring>
#include <ctime>
#include <stdexcept>

namespace bs {
namespace {

#include "boot/bios_entry_state.inc"

constexpr uint32_t kEntry = 0xAC010000;

} // namespace

BootFiles boot_read_data(const std::filesystem::path& dir) {
    BootFiles         f;
    disc::RuntimeDisc data(dir);  // lanca RuntimeDataError com a mensagem para o usuario
    try {
        f.ipBin              = data.ipBin();
        const disc::IpBin ip = disc::parseIpBin(f.ipBin.data(), f.ipBin.size());
        f.product = ip.product, f.version = ip.version, f.title = ip.title;
        const std::vector<uint8_t> raw = data.readFile(ip.boot_file);
        for (int pass = 0; pass < 2 && f.versionId.empty(); ++pass) {
            std::vector<uint8_t> bytes = pass ? disc::descramble(raw) : raw;
            const std::string    h     = util::sha256Hex(bytes.data(), bytes.size());
            for (const auto& v : kKnownVersions)
                if (h == v.boot_sha256) {
                    f.versionId = v.id;
                    f.firstRead = std::move(bytes);
                }
        }
    } catch (const disc::DiscError& err) {
        throw disc::RuntimeDataError(std::string("dados do jogo: ") + err.what());
    }
    if (f.versionId.empty())
        throw disc::RuntimeDataError("os dados do jogo nao sao da versao recompilada (hash do executavel de boot "
                                     "diferente). Gere a instalacao de novo com o Builder.");
    return f;
}

void boot_prepare(Sh4Context& c, const BootFiles& files, uint32_t rtcSeconds) {
    // Todo o hardware modelado volta ao reset: uma segunda sessao no mesmo processo nao
    // herda VRAM, interrupcoes pendentes, tempo do SPG, Maple, GD-ROM etc. (revisao Codex
    // 2026-10-14). A imagem de disco e a flash sao configuradas por quem chama.
    vram_reset();
    pvr_regs_reset();
    ta_reset();
    holly_intc_reset(0);
    holly_sb_reset();  // inclui o Maple
    gdrom_reset();
    aica_regs_reset();
    sh4_intc_reset();
    irq_bridge_reset();
    sh4_port_reset();
    sh4_sq_reset();
    sh4_dmac_reset();
    sh4_ccn_reset();
    sh4_tmu_reset();
    tmu_logical_reset();
    sys_logical_reset();
    uint8_t* ram = c.ram;
    std::memset(ram, 0, 16u << 20);
    std::memset(ram, 0xFF, 0x8000);  // area do sistema
    for (const auto& w : kBiosSysWords) std::memcpy(ram + (w[0] & 0xFFFFFF), &w[1], 4);
    std::memcpy(ram + 0x8000, files.ipBin.data(), files.ipBin.size());
    std::memcpy(ram + 0x10000, files.firstRead.data(), files.firstRead.size());
    std::memcpy(ram + 0x78, &rtcSeconds, 4);  // hora do sistema, como o BIOS a deixa

    sh4_reset(c, kEntry);
    // Registradores na entrada (medidos): pilha e VBR em 0x8C00F400, GBR na area do
    // sistema, PR de volta para o IP.BIN. Os demais valores sao restos do IP.BIN/BIOS,
    // reproduzidos para que o estado seja o mesmo do oraculo.
    c.r[15] = 0x8C00F400;
    c.vbr   = 0x8C00F400;
    c.gbr   = 0x8C000000;
    c.pr    = 0xAC00E0B2;
    c.macl  = 0x00007080;
    c.ssr   = 0x40000001;
    c.spc   = 0x8C000776;
    const uint32_t fr3 = 0x38D1B717;
    std::memcpy(&c.fr[3], &fr3, 4);
    const uint32_t bank[8] = {0x700000F0, 0xFFFFFF0F, 0xFF00001C, 0, 0xAC00FC00, 0, 0x400, 0x400};
    std::memcpy(c.rbank, bank, sizeof bank);
    c.ret_expect = c.pr;
    c.next_event = 0;
    c.next_poll  = ~0ull;  // N8.1: o primeiro atendimento decide (cadencia B no tempo logico)
    sh4_irq_bind(&c);      // N8.1: quem pede atendimento sem contexto (holly_intc_raise) escreve neste

    std::vector<uint32_t> pvr(2048, 0);
    for (const auto& r : kBiosPvrState) pvr[r[0] / 4] = r[1];
    pvr_regs_load(pvr.data());
    aica_rtc_set(rtcSeconds, 0);
    bios_hle_install(&c, true);
}

uint32_t boot_rtc_now() {
    // 1950-01-01 a 1970-01-01: 20 anos com 5 bissextos = 7305 dias
    constexpr int64_t kEpochDelta = 7305ll * 86400;
    std::time_t       t           = std::time(nullptr);
    std::tm           local{};
    localtime_s(&local, &t);
    // o RTC do Dreamcast guarda a hora local (configurada pelo usuario no BIOS)
    const int64_t localSecs = int64_t(t) + int64_t(_mkgmtime(&local) - t);
    return uint32_t(localSecs + kEpochDelta);
}

} // namespace bs
