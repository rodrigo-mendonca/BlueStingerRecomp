#pragma once
// HLE das syscalls do BIOS do Dreamcast (vetores em 0x8C0000B0..0x8C0000E0, system/system_area.h).
// N8.3: modelo de validacao e das ferramentas (bs_runtime_hle); o produto usa o servico nativo
// (system/syscall_service). Em cada endereco ha uma funcao nativa registrada no despacho (sh4_register_hle).
#include "cpu/sh4_context.h"
#include "system/system_area.h"

namespace bs {

// Registra as funcoes nativas nos enderecos dos vetores. Com `writeVectors`, tambem
// grava os vetores na RAM (boot nativo; num snapshot do Flycast eles ja estao la).
void bios_hle_install(Sh4Context* c, bool writeVectors);

} // namespace bs
