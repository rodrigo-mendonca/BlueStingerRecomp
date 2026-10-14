#pragma once
// HLE das syscalls do BIOS do Dreamcast (vetores em 0x8C0000B0..0x8C0000E0).
// Os vetores apontam para 0x8C001000+, os mesmos enderecos que o reios do Flycast
// usa (assim snapshots do oraculo e o boot nativo enxergam a mesma RAM); em cada
// um ha uma funcao nativa registrada no despacho (sh4_register_hle).
#include "cpu/sh4_context.h"

namespace bs {

constexpr uint32_t kBiosVecSysinfo  = 0x8C0000B0;
constexpr uint32_t kBiosVecRomfont  = 0x8C0000B4;
constexpr uint32_t kBiosVecFlashrom = 0x8C0000B8;
constexpr uint32_t kBiosVecGdrom    = 0x8C0000BC;
constexpr uint32_t kBiosVecGdrom2   = 0x8C0000C0;
constexpr uint32_t kBiosVecMisc     = 0x8C0000E0;

// Registra as funcoes nativas nos enderecos dos vetores. Com `writeVectors`, tambem
// grava os vetores na RAM (boot nativo; num snapshot do Flycast eles ja estao la).
void bios_hle_install(Sh4Context* c, bool writeVectors);

} // namespace bs
