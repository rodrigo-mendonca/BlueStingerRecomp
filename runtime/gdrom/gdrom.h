#pragma once
// Unidade de GD-ROM vista pelas syscalls do BIOS (HLE). Os comandos sao executados
// de forma sincrona em GDROM_EXEC_SERVER; os setores vem de uma fonte de disco.
#include "cpu/sh4_context.h"

#include <memory>
#include <vector>

namespace bs {

struct DiscTrack {
    uint32_t number = 0;     // 1..99
    uint32_t session = 0;    // 1..
    uint32_t start_fad = 0;  // FAD = LBA + 150
    uint32_t length = 0;     // setores
    uint32_t ctrl = 0;       // 4 = dados, 0 = audio (Q-subcode)
};

// Fonte dos setores do disco (dados de usuario, 2048 bytes por setor).
class DiscSource {
public:
    virtual ~DiscSource()                                   = default;
    virtual const std::vector<DiscTrack>& tracks() const   = 0;
    virtual bool                          read(uint32_t fad, uint8_t* out) = 0;
};

void gdrom_set_disc(std::unique_ptr<DiscSource> disc);
void gdrom_reset();
// Blocos de leitura entregues pelo tempo (diagnostico N2b: epoca de eventos do BS_CRI_DIAG).
uint64_t gdrom_event_count();
// Hash do estado interno do HLE do GD-ROM (diagnostico N2b: observador de efeitos).
uint64_t gdrom_state_hash();
// Tempo das leituras por DMA: false = instantaneo, true = modelo do reios (oraculo). O
// padrao vem de BS_GDROM_TIMING (instant | reios).
void gdrom_set_timing(bool reios);
// Avanca uma leitura por DMA em andamento ate o instante atual (chamado pelo poll central).
void gdrom_poll(Sh4Context* c);

// Syscall GDROM (r6 = 0): `func` = r7, argumentos r4/r5. Grava r0 so nas funcoes que
// devolvem valor (EXEC_SERVER, INIT_SYSTEM e RESET deixam r0 como estava).
void gdrom_syscall(Sh4Context* c, uint32_t func, uint32_t r4, uint32_t r5);

} // namespace bs
