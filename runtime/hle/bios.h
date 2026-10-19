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

// Area do sistema (0x8C000000-0x8C007FFF). No hardware e do BIOS (o jogo so a usa pelos vetores
// e pela sysinfo); aqui o boot nativo a monta (0xFF, mais as variaveis, vetores, stubs e a tabela
// 0x8C003174 medidos, bios_entry_state.inc) e o BIOS HLE e o dono. Dentro dela, longe dessas
// estruturas, fica a area de dados do runtime nativo: memoria guest que o host precisa expor ao
// jogo e que nao pertence a nenhuma estrutura do SDK. Reserva conferida no N3-2
// (docs/N3-2-RELATORIO.md): o jogo nao escreve na faixa (dumps de gameplay) e nao tem literal que
// aponte para ela; quem a usa confere a cada passada que ninguem mais a escreveu.
constexpr uint32_t kHleNativeAreaBase = 0x8C004000;
constexpr uint32_t kHleNativeAreaSize = 0x100;
// Alocacoes na area nativa:
constexpr uint32_t kHleNativePadInfo     = kHleNativeAreaBase;  // 0x78 B: info do controle da porta A (N3-2)
constexpr uint32_t kHleNativePadInfoSize = 0x78;

// Registra as funcoes nativas nos enderecos dos vetores. Com `writeVectors`, tambem
// grava os vetores na RAM (boot nativo; num snapshot do Flycast eles ja estao la).
void bios_hle_install(Sh4Context* c, bool writeVectors);

} // namespace bs
