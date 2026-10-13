#pragma once
// Snapshot de estado SH-4 (registradores + 16 MB de RAM) para testes
// diferenciais contra o Flycast (ARCHITECTURE.md secao 7). O mesmo formato e
// escrito por tools/dev/flycast_oracle.py. Snapshots contem memoria do jogo:
// ficam em snapshots/, fora do git.
//
// Arquivo: "BSS1" | u32 versao (1) | u32 tamanho da RAM | SnapshotRegs | RAM
#include "cpu/sh4_context.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bs {

struct SnapshotRegs {
    uint32_t r[16];
    uint32_t pc, pr, gbr, vbr, mach, macl, sr, fpul, fpscr;
    uint32_t fr[16];
    uint32_t ssr, spc;
    uint32_t rbank[8];
    uint32_t xf[16];
    uint32_t flags;  // bit 0: xf valido (o stub GDB do Flycast nao expoe XF)
};
static_assert(sizeof(SnapshotRegs) == 4 * (16 + 9 + 16 + 2 + 8 + 16 + 1));

constexpr uint32_t kSnapXfValid = 1;

struct Snapshot {
    SnapshotRegs         regs{};
    std::vector<uint8_t> ram;  // 16 MB

    static Snapshot load(const std::filesystem::path& p);
    void            save(const std::filesystem::path& p) const;

    // Carrega o estado no contexto (sem efeitos de troca de banco) e aplica o
    // modo de FPU do host. c.ram deve apontar para 16 MB; a RAM e copiada.
    void apply(Sh4Context& c) const;
    static Snapshot capture(const Sh4Context& c, uint32_t pc);
};

struct Divergence {
    std::string what;  // "r4", "fpscr", "ram 0x0C123456", ...
    uint32_t    expected = 0, actual = 0;
};

// Compara dois snapshots. XF so e comparado se ambos o tiverem.
std::vector<Divergence> compare(const Snapshot& expected, const Snapshot& actual, bool compare_pc = false);

} // namespace bs
