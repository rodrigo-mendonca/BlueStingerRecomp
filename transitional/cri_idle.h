#pragma once
// N2b-2 (--tempo experimental): ociosidade da thread 3 (servidor do CRI, laco 8C158840).
//
// E o criterio validado na investigacao (docs/N2b-PROPOSTA.md, 9.2: `BS_CRI_DIAG=pular6`), em
// forma de produto. A passada entre duas visitas ao back-edge 8C158860 e OCIOSA se:
//  - nenhum evento aconteceu nela (interrupcao aceita, troca de thread, VBlank, evento do Holly,
//    bloco de GD-ROM: a mesma epoca de eventos do diagnostico);
//  - nao mudou o valor de nenhuma palavra da RAM principal, fora da pilha morta da thread 3
//    ([r15 - 16 KB, r15)), dos medidores do TMU do CRI (0x8C1DD1C8-0x8C1DD23B) e do indice do
//    rodizio do gerenciador de streams (0x8C2DA97C);
//  - nao escreveu fora da RAM (MMIO, VRAM, AICA), nao descarregou store queue, o host nao
//    escreveu na RAM do guest e o estado do GD-ROM e do TMU nao mudou.
// Uma passada ociosa bloqueia a thread 3 ate o proximo evento (o tempo logico anda); o evento
// acorda a proxima.
//
// A RAM e observada pelo rastreamento de escrita do sistema (Windows: MEM_WRITE_WATCH), sem
// custo nas escritas da CPU: a cada visita, so as paginas escritas desde a anterior sao
// comparadas com uma copia. A diferenca para o detector de investigacao (que ve so as escritas
// da CPU) e conservadora: o sistema ve tambem as escritas do runtime e do host.
#include "cpu/sh4_context.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bs::transitional::cri {

// RAM do guest (16 MB) com rastreamento de escrita. Lanca std::runtime_error se o sistema nao
// oferece (o modo experimental nao tem alternativa silenciosa).
uint8_t* alloc_watched_ram(size_t bytes);
void     free_watched_ram(uint8_t* ram);

struct IdleStats {
    uint64_t visits = 0, idle = 0, eventDuring = 0, effectRam = 0, effectOther = 0, dirtyPages = 0;
};

class IdleDetector {
public:
    // `ram`: a RAM de alloc_watched_ram. Mutacao (teste negativo): `ignoreRam` nao olha a RAM.
    IdleDetector(uint8_t* ram, size_t bytes, bool ignoreRam);

    // Chamado a cada visita ao back-edge da thread 3: a passada desde a visita anterior foi
    // ociosa? A primeira visita nunca e (sem linha de base).
    bool idle(const Sh4Context* c);
    const IdleStats& stats() const { return stats_; }

private:
    bool ramChanged(uint32_t spLo, uint32_t spHi);

    uint8_t*             ram_;
    size_t               bytes_;
    bool                 ignoreRam_;
    bool                 primed_ = false;
    std::vector<uint8_t> shadow_;
    std::vector<void*>   pages_;
    uint64_t             epoch_ = 0, hw_ = 0, sq_ = 0, host_ = 0, gd_ = 0, tmu_ = 0;
    IdleStats            stats_;
};

} // namespace bs::transitional::cri
