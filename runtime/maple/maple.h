#pragma once
// Barramento Maple (registradores 0x005F6C00-0x005F6CFF do system block): DMA da
// tabela de comandos em SB_MDSTAR, respostas dos dispositivos gravadas nos buffers de
// recepcao e fim de DMA em ISTNRM bit 12. M4: controle padrao na porta A sem unidades
// de expansao (sem VMU); portas B-D vazias.
#include "cpu/sh4_context.h"

namespace bs {

constexpr uint32_t kIstMapleDma = 1u << 12;

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
uint64_t maple_dma_due();
void     maple_dma_take();

} // namespace bs
