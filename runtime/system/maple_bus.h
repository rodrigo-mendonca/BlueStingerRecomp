#pragma once
// N8.4 (docs/N8-4-PERIFERICOS.md): barramento de perifericos do produto. Com os controles e os saves nativos (pd e bu
// atendidos pelos adapters do N3/N4), nenhum dispositivo existe no barramento; a camada per do SDK (codigo recompilado)
// continua a sondar as portas a cada VBlank (disparo em 8C0975C0: MDSTAR, MDST; leitura do MDST em 8C0976DE). Este
// servico atende essa sondagem sem simular dispositivo nenhum, com o mesmo efeito do modelo do Maple sem controle
// (runtime/maple, so na validacao): os registradores 0x005F6C00-0x005F6CFF guardam o valor escrito; MDST = 1 percorre
// a tabela de descritores, grava 0xFFFFFFFF (sem resposta) em cada buffer de recepcao e agenda o fim do DMA no mesmo
// instante do modelo (100 ticks por bit), que acende ISTNRM bit 12; MDST le 1 ate o fim.
// Contrato medido (newgame_area1, 6 000 VBlanks: 5 982 DMAs): 4 descritores por DMA, DEVICE_INFO (0x01) ao dispositivo
// principal de cada porta, sem dados, padrao 0. Qualquer outro pedido (outro comando ou destino, dados, padrao, disparo
// por VBlank) pediria um dispositivo ou um modo que nao existe no produto: erro explicito (sh4_fatal), contado.
#include "cpu/sh4_context.h"

#include <cstdint>

namespace bs {

constexpr uint32_t kIstMapleDma = 1u << 12;

// Contadores do barramento (prova de fechamento do N3-2; sempre ligados, so somas).
struct MapleCounters {
    uint64_t dmas = 0;                                         // disparos (MDST = 1)
    uint64_t devInfo[4] = {}, getCond[4] = {}, other[4] = {};  // quadros enviados por porta
    uint64_t controllerResponses = 0;                          // quadros respondidos pelo controle da porta A
    uint64_t sourceReads = 0;                                  // consultas a fonte de entrada (read/connected)
};

// Fachada do barramento (system block, holly_intc, GameHost, adapter do pd): o provedor ativo. Padrao: o servico nativo;
// a validacao pode instalar o modelo do Maple (maple_bus_backend, runtime/maple).
struct PeriphBackend {
    void (*reset)();
    bool (*read)(Sh4Context*, uint32_t, uint32_t&);
    bool (*write)(Sh4Context*, uint32_t, uint32_t);
    void (*vblank_in)();
    void (*dma_take)();
    MapleCounters (*counters)();
    uint64_t (*cond_seq)(int);
};
void          periph_use(const PeriphBackend* b);  // nullptr = servico nativo
void          periph_reset();
bool          periph_read(Sh4Context* c, uint32_t a, uint32_t& v);
bool          periph_write(Sh4Context* c, uint32_t a, uint32_t v);
void          periph_vblank_in();  // entrega do VBlank-In (indice "vblank"/"transicao" dos roteiros)
void          periph_dma_take();   // fim do DMA entregue (holly_intc)
MapleCounters periph_counters();
uint64_t      periph_cond_seq(int port);  // GET_CONDITION da porta (0 no servico nativo: sem controle)

// Pedidos fora do contrato no servico nativo (erro explicito; meta 0).
uint64_t maple_bus_refused();

} // namespace bs
