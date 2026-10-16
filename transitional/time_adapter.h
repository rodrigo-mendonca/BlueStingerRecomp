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
    // o relogio consultavel (TMU, SPG, MDST) andar em degraus de 1/8 de campo. N2b-0/1:
    // "perder_vblank_in" e "vblank_extra" (IRQ ausente/extra a cada 97 VBlank-In),
    // "fundir_vblank" (dois VBlank-In num atendimento), "render_perdido" (o 500o fim de render
    // nao chega: a espera de render vai ao timeout), "lento" (2 ms de host por VBlank) e
    // "sem_espera" (o monitor ignora a espera do VBlank do jogo). Vazio desliga.
    std::string mutation;
    // N2b-1 (modo sombra): BS_PROGRESS_TRACE grava inicio/avanco/fim de cada episodio de espera
    // e as faltas de progresso. Vazio desliga.
    std::string progress_trace;
    // D8: limite da janela de trabalho sem espera (pontos de controle); 0 = so mede. Em modo
    // sombra a falta de progresso so e registrada; nunca muda a execucao.
    uint64_t stall_units = 0;
};

// D8: limite padrao da janela de trabalho sem espera (pontos de controle), calibrado no N2b-1
// (docs/N2b-RELATORIO.md): a maior janela legitima medida (cargas de sala, roteiros NEW GAME e
// exploracao, 8 000 VBlanks) foi 17 232 307; quadros normais ficam abaixo de 1 000 000. Margem
// de ~3,7x. Em modo sombra a falta de progresso so e registrada.
constexpr uint64_t kDefaultStallUnits = 64000000;

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
    uint64_t                vblankCollapsed  = 0;  // dois VBlank-In entregues no mesmo atendimento (B4)
};
Stats stats();
// N2b-1: resumo do monitor de progresso (esperas, decisoes, janelas, faltas de progresso).
std::string progress_summary();

} // namespace bs::transitional::time
