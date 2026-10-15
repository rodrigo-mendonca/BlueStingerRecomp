#pragma once
// N0: trace da ordem de eventos do runtime legado (docs/PLANO-PORT-NATIVO.md secao 5).
//
// Com BS_EVENT_TRACE=<arquivo>, grava em ordem: VBlank, interrupcao aceita, continuacao por
// rte, troca e criacao de thread do guest, longjmp. O trace e a referencia que o relogio e o
// agendador nativos (N2) tem de reproduzir: a ordem observada, nao so o estado final.
// Desligado, custa um teste de bool em cada um desses pontos (raros: dezenas por quadro).
#include <cstdint>
#include <string>

namespace bs {

extern bool g_event_trace;

enum class TraceEvent : char {
    VBlank       = 'V',  // a = quadro (contagem de VBlank-In)
    Irq          = 'I',  // a = INTEVT, b = SPC (ponto interrompido)
    RteContinue  = 'R',  // a = destino (rotina adiada pela interrupcao)
    ThreadSwitch = 'T',  // a = thread de origem, b = thread de destino
    ThreadNew    = 'N',  // a = indice da nova thread, b = entrada
    Longjmp      = 'J',  // a = destino, b = thread de destino
};

void event_trace(TraceEvent e, uint64_t cycles, uint32_t a, uint32_t b = 0);

void        event_trace_init(const std::string& path);  // vazio desliga
bool        event_trace_flush();
std::string event_trace_dump();  // testes

} // namespace bs
