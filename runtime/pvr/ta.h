#pragma once
// Tile Accelerator (TA) do PowerVR2: recebe as listas de exibicao (parametros de
// 32/64 bytes) pela FIFO na area 4, separa por lista e sinaliza o fim de cada lista.
#include "cpu/sh4_context.h"

#include <vector>

namespace bs {

// Um parametro do TA (palavra de controle + dados), 8 ou 16 palavras.
struct TaParam {
    uint32_t list = 0;   // 0 opaca, 1 modificador opaco, 2 translucida, 3 modificador transl., 4 punch-through
    uint32_t words = 0;  // 8 ou 16
    uint32_t w[16] = {};
};

void                         ta_reset();
void                         ta_list_init();      // TA_LIST_INIT: comeca listas novas
void                         ta_list_continue();  // TA_LIST_CONT: continua as listas atuais
const std::vector<TaParam>&  ta_params();         // parametros recebidos desde o ultimo init
uint32_t                     ta_bytes();          // bytes recebidos pela FIFO desde o ultimo init

// Destino das store queues/DMA na area 4: FIFO de poligonos (0x10000000), caminho direto
// de textura (0x11000000, escreve na VRAM pelo caminho de 64 bits). 32 bytes por vez.
bool ta_fifo_write(Sh4Context* c, uint32_t target, const uint32_t* data);

} // namespace bs
