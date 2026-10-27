#pragma once
// N8.3 (aprovado pelo mantenedor em 2026-10-26, opcao A; docs/N8-3-BOOT.md): servico nativo das syscalls do sistema.
// O jogo salta pelos vetores da area do sistema (system_area.h) a partir de 21 stubs do SDK (13 do GD-ROM em
// 8C094DEC-8C094EEC, 3 da SYSINFO em 8C099510+, 4 da FLASHROM em 8C09A208+, 1 do MISC em 8C15E9D6). O produto nao
// tem BIOS, drive de GD-ROM nem flash simulados: este servico atende, nos mesmos enderecos e com o mesmo efeito na RAM e
// nos registradores, so o contrato medido do jogo (com o provedor nativo de arquivos do N1):
//  - GDROM (r6 = 0): INIT_SYSTEM, GET_DRV_STAT (pausa, GD-ROM), REQ_CMD, GET_CMD_STAT e EXEC_SERVER, com os comandos
//    INIT (0x18), GET_VERSION (0x28) e leitura por DMA (0x11), que termina inteira no EXEC_SERVER. As leituras sao
//    setores dos dados do jogo (o RuntimeDisc nativo; no jogo, 2 setores de metadados do volume no boot);
//  - FLASHROM: so a particao 2 (0x1C000, 16 KB): INFO, READ, WRITE (a flash so apaga bits: AND) e DELETE, em memoria,
//    em branco (0xFF) a cada sessao (decisao do mantenedor: volatil, como as referencias);
//  - SYSINFO: so ID (ponteiro para 0x8C000068);
//  - MISC r4 = 1 (8C15E9C4, o soft reset A+B+X+Y+START): o menu do sistema. O console sairia do jogo para o menu do
//    BIOS; aqui a sessao termina pelo gancho do host (syscall_service_set_menu_hook), com mensagem. Sem gancho, erro.
// Qualquer outro pedido (outra funcao, comando, particao, faixa ou vetor) e um componente removido do produto: erro
// explicito (sh4_fatal), contado, sem recuo silencioso. O modelo HLE (hle/bios, gdrom/gdrom, hle/flashrom) fica so na
// validacao (--arquivos legado, --flash, --sistema hle, BS_GDROM_TIMING) e nas ferramentas.
// O estado do pedido e o hash (sys_disc_state_hash) sao os do modelo no perfil instantaneo: o detector de ociosidade
// da thread 3 (cri_idle) e o diagnostico do CRI enxergam as mesmas mudancas.
// Diagnostico: BS_SISTEMA_LOG=1 registra cada chamada (como o BS_HLE_LOG do modelo).
#include "cpu/sh4_context.h"

#include <cstdint>
#include <memory>

namespace bs {
namespace disc {
class RuntimeDisc;
}

// Registra o servico nos enderecos dos vetores e grava os vetores na RAM (boot nativo).
void syscall_service_install(Sh4Context* c);
// Estado dos pedidos e flash em branco (boot de cada sessao).
void syscall_service_reset();
// Dados do jogo para as leituras de setor (nullptr fecha).
void syscall_service_set_data(std::unique_ptr<disc::RuntimeDisc> data);

// Fachada dos consumidores do estado do drive (poll central, cri_idle, diagnostico do CRI): o provedor ativo.
struct SysDiscBackend {
    void (*poll)(Sh4Context*);
    uint64_t (*state_hash)();
    uint64_t (*event_count)();
};
void     sys_disc_use(const SysDiscBackend* b);  // nullptr = servico nativo (padrao)
void     sys_disc_poll(Sh4Context* c);
uint64_t sys_disc_state_hash();
uint64_t sys_disc_event_count();

// Cobertura do contrato (testes e diagnostico).
struct SyscallCounts {
    uint64_t gdInit = 0, gdDrvStat = 0, gdReqCmd = 0, gdCmdStat = 0, gdExecServer = 0;
    uint64_t cmdInit = 0, cmdVersion = 0, cmdDmaRead = 0, sectors = 0;
    uint64_t flashInfo = 0, flashRead = 0, flashWrite = 0, flashDelete = 0;
    uint64_t sysinfoId = 0;
    uint64_t menuExit = 0;  // MISC r4 = 1: o jogo pediu o menu do sistema (soft reset)
    uint64_t refused = 0;  // pedidos fora do contrato (erro explicito; meta 0)
};
const SyscallCounts& syscall_service_counts();
// Gancho do menu do sistema (MISC r4 = 1): o host encerra a sessao (nao volta; nullptr tira).
using MenuHook = void (*)(Sh4Context*);
void syscall_service_set_menu_hook(MenuHook hook);

} // namespace bs
