#pragma once
// Area do sistema do Dreamcast (0x8C000000-0x8C007FFF), como o jogo a enxerga (N8.3, docs/N8-3-BOOT.md).
// No hardware e do BIOS; aqui o boot nativo a monta (0xFF, mais as variaveis, vetores, stubs e a tabela
// 0x8C003174 medidos, bios_entry_state.inc). O jogo so a usa pelos vetores das syscalls e pela sysinfo.
#include <cstdint>

namespace bs {

// Vetores das syscalls (o jogo salta por eles: mov.l @vetor,r0; jmp @r0)
constexpr uint32_t kBiosVecSysinfo  = 0x8C0000B0;
constexpr uint32_t kBiosVecRomfont  = 0x8C0000B4;
constexpr uint32_t kBiosVecFlashrom = 0x8C0000B8;
constexpr uint32_t kBiosVecGdrom    = 0x8C0000BC;
constexpr uint32_t kBiosVecGdrom2   = 0x8C0000C0;
constexpr uint32_t kBiosVecMisc     = 0x8C0000E0;

// Para onde os vetores apontam: os mesmos enderecos que o reios do Flycast usa (snapshots do oraculo e o boot nativo
// enxergam a mesma RAM). Em cada um ha uma funcao nativa registrada no despacho (sh4_register_hle): o servico nativo
// (system/syscall_service) no produto, o modelo HLE (hle/bios) so na validacao e nas ferramentas.
constexpr uint32_t kSyscallSysinfo  = 0x8C001000;
constexpr uint32_t kSyscallRomfont  = 0x8C001002;
constexpr uint32_t kSyscallFlashrom = 0x8C001004;
constexpr uint32_t kSyscallGdrom    = 0x8C001006;
constexpr uint32_t kSyscallMisc     = 0x8C001008;
constexpr uint32_t kSyscallGdrom2   = 0x8C0010F0;

// SYSINFO ID: ponteiro para o ID do sistema (o boot nativo deixa a area como o BIOS a deixou)
constexpr uint32_t kSysinfoData = 0x8C000068;

// Dentro da area, longe dessas estruturas, fica a area de dados do runtime nativo: memoria guest que o host precisa
// expor ao jogo e que nao pertence a nenhuma estrutura do SDK. Reserva conferida no N3-2 (docs/N3-2-RELATORIO.md): o
// jogo nao escreve na faixa (dumps de gameplay) e nao tem literal que aponte para ela; quem a usa confere a cada
// passada que ninguem mais a escreveu.
constexpr uint32_t kHleNativeAreaBase = 0x8C004000;
constexpr uint32_t kHleNativeAreaSize = 0x100;
// Alocacoes na area nativa:
constexpr uint32_t kHleNativePadInfo     = kHleNativeAreaBase;  // 0x78 B: info do controle da porta A (N3-2)
constexpr uint32_t kHleNativePadInfoSize = 0x78;

} // namespace bs
