#pragma once
// N8.7 fase 0 (docs/N8-7-KAMUI.md secao 8): medicao da fronteira grafica guest, so no build de validacao.
//
// Com BS_HW_PROBE_GRAFICOS=<arquivo.txt>, mede, sem mudar o comportamento:
//  - os blocos de 32 bytes que chegam a area 4 (TA) por store queue e pelo DMA do canal 2, separados entre a FIFO de
//    poligonos, o caminho direto de textura e o YUV, por funcao do guest; os bytes de poligono por render;
//  - cada transferencia do canal 2 (funcao, origem, destino, tamanho);
//  - os acessos aos registradores do PVR (005F8000), do INTC do Holly (005F6900) e do bloco do sistema (005F6800):
//    leituras e escritas por funcao e registrador, com os valores lidos distintos (amostra);
//  - no STARTRENDER: FB_W_SOF1/2, FB_W_CTRL, FB_W_LINESTRIDE e os FB_R_SOF1/2 da saida de video; os alvos de render que
//    nunca foram exibidos e as texturas amostradas dentro de um alvo de render (render para textura);
//  - o tratador de interrupcoes do Kamui (8C1C53E2, chamado pelo despachante 8C1982F4): a sequencia de acessos
//    (registrador, leitura/escrita, valor) de cada episodio, agregada por sequencia.
// Ao sair, grava o resumo. No produto a variavel e recusada (debug/validacao_stubs.cpp).
#include "cpu/sh4_context.h"

#include <cstdint>
#include <string>

namespace bs {

extern bool g_gfx_probe;

enum class GfxPath : char { StoreQueue = 'q', Dma = 'd' };

void gfx_probe_init(const std::string& path);
bool gfx_probe_flush();
// um bloco de 32 bytes para a area 4 (target fisico)
void gfx_probe_ta(const Sh4Context* c, uint32_t target, GfxPath path);
// transferencia do canal 2 aceita (antes de copiar)
void gfx_probe_dma(const Sh4Context* c, uint32_t src, uint32_t dest, uint32_t len);
// registradores 005F6800-005F9FFF (fisico), leitura ou escrita de 32 bits
void gfx_probe_reg(const Sh4Context* c, uint32_t reg, bool write, uint32_t v);
// escrita na VRAM pelo caminho de memoria (fisico 04xxxxxx/05xxxxxx)
void gfx_probe_vram_write(const Sh4Context* c, uint32_t addr, unsigned size);
// STARTRENDER e TA_LIST_INIT
void gfx_probe_start_render(const Sh4Context* c);
void gfx_probe_list_init();
// a textura de um render (TCW e dimensoes), para o teste de render para textura
void gfx_probe_texture(uint32_t tcw, uint32_t width, uint32_t height);

} // namespace bs
