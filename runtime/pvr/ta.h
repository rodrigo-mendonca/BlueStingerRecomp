#pragma once
// Tile Accelerator (TA) do PowerVR2: recebe as listas de exibicao (parametros de
// 32/64 bytes) pela FIFO na area 4, separa por lista e sinaliza o fim de cada lista.
#include "cpu/sh4_context.h"
#include "render/kamui/list_sink.h"

#include <string>
#include <vector>

namespace bs {

// Um parametro do TA (palavra de controle + dados), 8 ou 16 palavras, e a lista (0 opaca, 1 modificador opaco,
// 2 translucida, 3 modificador transl., 4 punch-through). N8.7: o tipo do ListSink nativo.
using TaParam = native::render::kamui::ListParam;

void                         ta_reset();
void                         ta_list_init();      // TA_LIST_INIT: comeca listas novas
void                         ta_list_continue();  // TA_LIST_CONT: continua as listas atuais
const std::vector<TaParam>&  ta_params();         // parametros recebidos desde o ultimo init
uint32_t                     ta_bytes();          // bytes recebidos pela FIFO desde o ultimo init
// N8.7 (docs/N8-7-KAMUI.md): o destino nativo das mesmas listas (recebe cada bloco da FIFO de poligonos)
native::render::kamui::ListSink& ta_list_sink();
// N8.7, validacao: fins de lista em que o ListSink e a FIFO do legado (a referencia) discordaram (tem de ficar em 0)
uint64_t ta_list_end_mismatches();
// N8.7 4c, so a validacao: a entrada da area 4 nativa em sombra no TA do legado ("[area-4] ...\n"); vazio no produto
std::string ta_area4_summary();

// Destino das store queues/DMA na area 4: FIFO de poligonos (0x10000000), caminho direto
// de textura (0x11000000, escreve na VRAM pelo caminho de 64 bits). 32 bytes por vez.
bool ta_fifo_write(Sh4Context* c, uint32_t target, const uint32_t* data);

// Conversor YUV (0x10800000): escrever TA_YUV_TEX_BASE ou TA_YUV_TEX_CTRL recomeca a
// conversao com os valores atuais; TA_YUV_TEX_CNT le ta_yuv_count().
void     ta_yuv_reset(uint32_t base, uint32_t ctrl);
void     ta_yuv_write(Sh4Context* c, const uint32_t* data);  // 32 bytes de macrobloco
uint32_t ta_yuv_count();
// N8.5: no produto (sem BS_VALIDACAO) o conversor YUV e um componente removido: erro explicito (so o filme legado o usa).
[[noreturn]] void ta_yuv_removed(Sh4Context* c, const char* what, uint32_t where);

} // namespace bs
