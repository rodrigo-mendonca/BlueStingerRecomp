#pragma once
// Barramento Maple (registradores 0x005F6C00-0x005F6CFF do system block): DMA da
// tabela de comandos em SB_MDSTAR, respostas dos dispositivos gravadas nos buffers de
// recepcao e fim de DMA em ISTNRM bit 12. M4: controle padrao na porta A sem unidades
// de expansao (sem VMU); portas B-D vazias.
// N8.4: modelo de validacao e das ferramentas (bs_runtime_hle); o produto usa o barramento sem dispositivos
// (system/maple_bus), que reproduz este modelo sem controle. Os consumidores leem pela fachada periph_*.
#include "cpu/sh4_context.h"
#include "system/maple_bus.h"

namespace bs {

void maple_reset();

// Estado dos botoes do controle da porta A (ativo em 0, como no hardware: 0xFFFF = nada
// apertado), gatilhos e analogicos (0x80 = centro).
struct MapleControllerState {
    uint16_t buttons = 0xFFFF;
    uint8_t  rtrig = 0, ltrig = 0;
    uint8_t  joyx = 0x80, joyy = 0x80, joy2x = 0x80, joy2y = 0x80;
};
void maple_set_controller(const MapleControllerState& s);

// true se `a` e registrador do Maple (o acesso foi tratado).
bool maple_read(Sh4Context* c, uint32_t a, uint32_t& v);
bool maple_write(Sh4Context* c, uint32_t a, uint32_t v);

// Fim de DMA pendente (ciclo, 0 = nenhum). holly_intc acende ISTNRM bit 12 e chama take.
// Pedidos de condicao (GetCond) feitos ao controle da porta: o indice do roteiro de entrada.
uint64_t maple_cond_seq(int port);
// Entrega do VBlank-In (o host chama antes do tratador do jogo): ponto de amostragem alternativo
// dos negativos do N3-0 e indice dos roteiros "indice vblank". Sem efeito no padrao.
void     maple_vblank_in();
uint64_t maple_dma_due();
void     maple_dma_take();

// N3-2 (--controles nativo): sem controle na porta A, como BS_MAPLE_PORTS=none. O DMA da camada
// per continua (sondagem das portas; a VMU usa a mesma camada ate o N4), mas nenhum dispositivo
// responde e a fonte de entrada nunca e lida. Padrao: com controle. maple_reset nao muda isto.
void maple_set_controller_present(bool present);
bool maple_controller_present();

MapleCounters maple_counters();
void          maple_counters_reset();
// Este modelo como provedor da fachada periph_* (validacao: --controles legado, --saves legado, negativos).
const PeriphBackend* maple_bus_backend();

} // namespace bs
