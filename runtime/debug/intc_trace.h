#pragma once
// N8.1 (passo 0): trace observacional das fontes de interrupcao do Holly, das leituras de relogio e das operacoes do
// cartao, no mesmo eixo de ordem do BS_EVENT_TRACE (docs/N8-0-REFERENCIAS.md secao 6). Nada aqui muda a execucao.
//
// BS_INTC_TRACE=<arquivo> liga (com BS_EVENT_TRACE para a coluna `pos`). Tipos:
//   H  bits do ISTNRM acesos: fonte `update` (eventos vencidos do provedor de tempo) ou `raise` (fonte sincrona);
//      a = bits que acenderam agora, b = bits pedidos (um bit pedido ja aceso e fusao); n = origem do sh4_service em
//      curso (0 = fora dele: acesso a registrador ou BS_IRQ_CHECK; senao o codigo da fonte de E abaixo)
//   A  bits limpos pelo tratador (escrever 1 limpa): fonte ISTNRM/ISTEXT/ISTERR; a = bits que estavam acesos e
//      apagaram, b = valor escrito
//   C  leituras de relogio e de estado (TMU, SPG, MDST, RTC, IST): leituras seguidas da mesma fonte, sem outro
//      registro no meio (deste trace ou do BS_EVENT_TRACE), viram uma linha: a = primeiro valor, b = ultimo, n = leituras
//   L  cartao nativo (bu_adapter): fonte load/save pedido ou fim; a = resultado, b = unidade
//   E  interrupcao entregue (a mesma do `I` do BS_EVENT_TRACE, gravada logo antes dele): fonte = de onde veio a
//      entrega: `cadencia` (poll pelo quantum de ciclos), `forcado` (next_event = 0: espera do tempo experimental, fim
//      do DMA do canal 2), `ldc_sr` (BS_IRQ_CHECK), `hardware` (espera do hardware), `sleep`; a = INTEVT, b = SPC,
//      n = SR
//   S  sh4_service da cadencia (quantum de ciclos) que teve efeito: a = eventos do Holly novos, b = entregas,
//      n = blocos do GD-ROM. Mede o que depende do quantum de ciclos (N8.1: cadencia B)
// Colunas: pos (eventos do BS_EVENT_TRACE gravados antes deste registro: a ordem relativa exata), passo de jogo,
// VBlank-In, ciclos (instante do provedor nos H e A; 0 nos outros: a catraca tempo.consumidores.ciclos nao admite
// leitura nova do contador de ciclos fora do diagnostico conhecido; a ordem vem de `pos`), tipo, fonte, a, b, ISTNRM depois, IML6NRM,
// IML4NRM, IML2NRM, n. Desligado custa um teste de bool por ponto.
#include <cstdint>
#include <string>

namespace bs {

extern bool g_intc_trace;

enum class IntcSource : uint8_t {
    Update, Raise,                      // H
    IstNrm, IstExt, IstErr,             // A
    Tmu, Spg, Mdst, Rtc, Ist,           // C
    LoadReq, LoadEnd, SaveReq, SaveEnd, // L
    Cadence, Forced, IrqCheck, Hardware, Sleep,  // E (origem da entrega)
    Count
};

void intc_trace_holly(char kind, IntcSource src, uint64_t cycles, uint32_t a, uint32_t b, uint32_t ist, uint32_t iml6,
                      uint32_t iml4, uint32_t iml2);
void intc_trace_clock(IntcSource src, uint32_t value);
void intc_trace_card(IntcSource src, uint32_t result, uint32_t drive);
void intc_trace_delivery(IntcSource origin, uint64_t cycles, uint32_t intevt, uint32_t spc, uint32_t sr);
void intc_trace_service(IntcSource origin, uint64_t cycles, uint32_t holly_events, uint32_t deliveries, uint64_t blocks);
void intc_trace_set_origin_fn(uint8_t (*fn)());  // origem do sh4_service em curso (registro H)
void intc_trace_step(uint64_t step);  // passo de jogo corrente (o provedor de tempo chama a cada passo)

void        intc_trace_init(const std::string& path);  // vazio desliga
bool        intc_trace_flush();
std::string intc_trace_dump();  // testes

} // namespace bs
