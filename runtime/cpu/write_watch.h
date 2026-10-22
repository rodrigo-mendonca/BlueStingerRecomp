#pragma once
// N2b (P1b, diagnostico): registro dos efeitos de um trecho do guest (p.ex. uma passada do
// servidor do CRI). So registra no build de investigacao -DBS_WRITE_WATCH=ON, que compila o
// caminho de escrita com a sonda; no build normal (produto) as funcoes nao sao chamadas e a
// estrutura so existe para o diagnostico ler "desligado".
//  - ramWrites/ramChanges: escritas da CPU na RAM principal fora das exclusoes (pilha morta e
//    faixas de diagnostico), e quantas mudaram o valor naquele momento (inclui mudancas
//    transitorias, desfeitas antes do fim do trecho);
//  - orig: o valor original de cada palavra escrita, para a mudanca LIQUIDA no fim do trecho
//    (ww_net_changes);
//  - hwWrites: escritas fora da RAM (MMIO, VRAM, store queues, AICA), pelo hwWrite;
//  - hostWrites: escritas do host na RAM do guest (GuestMemory::write: provedores nativos);
//  - loops: os lacos ativos mais recentes de BS_WW_VOLTAS (ww_poll_note). Toda escrita (RAM fora
//    da pilha abaixo do r15 de cada laco, hardware, store queue, host) marca "dirty" os lacos
//    para os quais ela conta, para a "volta sem progresso".
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace bs {

struct WwLoop {
    uint32_t lo = 0, hi = 0;  // pilha abaixo do r15 do laco (frames das chamadas dentro da volta)
    bool     dirty = true;    // houve escrita relevante desde a ultima volta deste laco
};
constexpr int kWwLoops = 16;

struct WriteWatch {
    bool     on = false;
    uint32_t spLo = 0, spHi = 0;          // pilha morta excluida (deslocamentos na RAM)
    uint32_t exLo[4] = {}, exHi[4] = {};  // outras exclusoes (deslocamentos), faixa vazia = livre
    uint64_t ramWrites = 0, ramChanges = 0, hwWrites = 0, hostWrites = 0, sqFlushes = 0;
    bool     track = false;  // BS_WW_VOLTAS ativo
    WwLoop   loops[kWwLoops];
    std::unordered_map<uint32_t, uint32_t> orig;  // palavra alinhada -> valor antes da 1a escrita
};

extern WriteWatch g_ww;

#if defined(BS_WRITE_WATCH) && BS_WRITE_WATCH
constexpr bool kWriteWatchBuilt = true;
#else
constexpr bool kWriteWatchBuilt = false;
#endif

// Escrita fora da RAM (hardware, store queue) ou do host: conta para todos os lacos.
inline void ww_dirty_all() {
    if (g_ww.track)
        for (auto& l : g_ww.loops) l.dirty = true;
}

inline void ww_ram(const uint8_t* dst, uint32_t off, const void* src, size_t n) {
    if (g_ww.track)
        for (auto& l : g_ww.loops)
            if (off < l.lo || off >= l.hi) l.dirty = true;
    if (!g_ww.on || (off >= g_ww.spLo && off < g_ww.spHi)) return;
    for (int i = 0; i < 4; ++i)
        if (off >= g_ww.exLo[i] && off < g_ww.exHi[i]) return;
    ++g_ww.ramWrites;
    if (std::memcmp(dst, src, n) != 0) ++g_ww.ramChanges;
    const uint32_t w = off & ~3u;  // escritas alinhadas: cabem numa palavra
    if (!g_ww.orig.count(w)) {
        uint32_t old;
        std::memcpy(&old, dst - (off - w), 4);
        g_ww.orig.emplace(w, old);
    }
}

// Contagem exata de pontos de poll por PC de retomada (BS_WW_VOLTAS=<arquivo>; colunas pc, voltas
// de back-edge, retornos de chamada, voltas sem progresso):
//  - num back-edge, o PC de retomada e o inicio do laco, e a contagem e o numero de voltas;
//  - os polls de retorno de chamada (BS_POLL_RET) contam a parte, porque um retorno pode cair no
//    mesmo PC de um inicio de laco;
//  - volta SEM PROGRESSO: back-edge com o mesmo PC de retomada que a volta anterior DO MESMO
//    laco (mesmo que outros lacos rodem no meio, p.ex. dentro de uma chamada), com todo o estado
//    arquitetural da CPU igual (R0-R15, banco R0-R7, SR com T, GBR, VBR, SSR, SPC, SGR, DBR, PR,
//    FR/XF, FPSCR, FPUL, MACH, MACL) e nenhuma escrita relevante entre as duas, fora a pilha
//    abaixo do r15 do laco (64 KB). Um laco deterministico nesse estado repetiria para sempre; so
//    sai por mudanca externa (hardware, interrupcao, DMA, outra thread). A exclusao da pilha so
//    pode gerar falso POSITIVO (uma escrita viva ali escondida): toda deteccao e conferida a mao.
// Com BS_WW_DECLARAR=1, as esperas temporais do boot (8C172624, 8C17262A, 8C172804, 8C172EF2 e a
// linha 0 do SPG em 8C1C6CB0/8C1C6D08) sao informadas ao monitor como pontos de espera (modo
// sombra: a execucao nao muda).
struct Sh4Context;
void ww_poll_note(Sh4Context* c, uint32_t resume, bool callReturn);

// N7c (investigacao da camera): BS_ESCRITOR=<lo>-<hi>:<arquivo> registra QUEM escreve na faixa de enderecos (RAM do
// guest, inclusive): a funcao em execucao (c->cur_fn, a ultima entrada; depois de uma chamada ainda e a chamada) e o
// PR (endereco de retorno: o chamador da funcao atual), com o valor. Agrega por (endereco, funcao, PR) e grava as
// primeiras escritas uma a uma. So no build de investigacao (BS_WRITE_WATCH).
struct WwWriter {
    bool     on = false, init = false;
    uint32_t lo = 0, hi = 0;  // deslocamentos na RAM
};
extern WwWriter g_wwWriter;
void ww_writer_init();
void ww_writer(const Sh4Context* c, uint32_t off, uint32_t value, int bytes);
// O mesmo para LEITURAS: BS_LEITOR=<lo>-<hi>:<arquivo> (quem le a faixa; agregado por endereco, funcao e PR).
extern WwWriter g_wwReader;
void ww_reader(const Sh4Context* c, uint32_t off, int bytes);

// Palavras cujo valor no fim do trecho difere do original (mudanca liquida).
inline uint64_t ww_net_changes(const uint8_t* ram) {
    uint64_t n = 0;
    for (const auto& [w, old] : g_ww.orig) {
        uint32_t now;
        std::memcpy(&now, ram + w, 4);
        n += now != old;
    }
    return n;
}

} // namespace bs
