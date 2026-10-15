#pragma once
// N2a (docs/N2-PLANO.md): provedor de tempo nativo do runtime (--tempo nativo).
//
// O relogio logico (native::time::LogicalClock) e as filas de eventos (EventQueue) passam a
// dizer quando VBlank-In/Out, fim de render, fim de DMA do Maple e os blocos de leitura do
// GD-ROM vencem; TMU, SPG_STATUS, MDST e o RTC leem o relogio. A entrega a CPU continua nos
// pontos de sempre (poll, acesso aos registradores, nivel IRL): o comportamento tem de ser o
// mesmo do provedor legado.
//
// Durante o N2a o progresso ainda e o contador de ciclos estimados: ele entra aqui so como
// instante de compatibilidade (um tick por ciclo, kCompatTicksPerField por campo) e nao
// aparece na API nativa. Dois dominios de entrega, como antes: o do Holly (Holly/PVR/Maple) e
// o do GD-ROM, cada um sincronizado nos seus pontos.
#include <array>
#include <cstdint>
#include <string>

namespace bs::transitional::time {

struct Options {
    // BS_TIME_TRACE: cada entrega (tipo, instante devido, instante de entrega), para o
    // criterio A2. Vazio desliga.
    std::string trace;
    // BS_TEMPO_MUTACAO (so testes negativos; cada uma introduz um defeito que os portoes tem
    // de pegar): "perder_vblank_out" descarta os VBlank-Out; "atrasar_render" entrega o fim de
    // render um quantum depois; "ignorar_cancelamento" deixa na fila os eventos cancelados;
    // "deriva" atrasa cada VBlank-In 1 000 ticks a mais que o anterior; "relogio_degraus" faz
    // o relogio consultavel (TMU, SPG, MDST) andar em degraus de 1/8 de campo. Vazio desliga.
    std::string mutation;
};

// Instala o provedor nativo no runtime (antes de boot_prepare). Lanca std::invalid_argument
// para mutacao desconhecida ou trace que nao abre.
void install(const Options& opt);
// Volta ao provedor legado e fecha o trace.
void uninstall();
bool installed();

struct Stats {
    std::array<uint64_t, 8> delivered{};  // por native::time::EventKind
    uint64_t                posted = 0, cancelled = 0;
    uint64_t                clockRegressions = 0;  // instante de compatibilidade menor que o relogio
};
Stats stats();

} // namespace bs::transitional::time
