// Syscall GDROM do BIOS (r6 = 0). Funcoes (r7) e comandos conforme a documentacao do
// KallistiOS (dc/cdrom) e o comportamento do reios do Flycast (so referencia):
//   0 REQ_CMD(r4 = comando, r5 = parametros[4]) -> id do pedido (> 0) ou 0 se ocupado
//   1 GET_CMD_STAT(r4 = id, r5 = estado[4])      -> 0 ocioso, 1 ocupado, 2 concluido, -1 erro
//   2 EXEC_SERVER                                 -> executa o pedido pendente
//   3 INIT_SYSTEM, 4 GET_DRV_STAT(r4 = [estado, tipo do disco]), 9 RESET
// Um pedido por vez; o comando e executado na chamada a EXEC_SERVER (inteiro, ou em
// blocos de tempo com BS_GDROM_TIMING=reios).
#include "gdrom/gdrom.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bs {
namespace {

enum : uint32_t { kReqCmd = 0, kGetCmdStat = 1, kExecServer = 2, kInitSystem = 3, kGetDrvStat = 4, kReset = 9 };
enum : int32_t { kErr = -1, kOk = 0, kBusy = 1, kComplete = 2 };
constexpr uint32_t kStatPause  = 1;     // gd_drv_stat
constexpr uint32_t kDiscGdrom  = 0x80;  // tipo de disco: GD-ROM (o IP.BIN declara GD-ROM)

struct State {
    int32_t  status = kOk;
    uint32_t lastId = 0xFFFFFFFF, nextId = 1;
    uint32_t command = 0;
    uint32_t params[4] = {};
    uint32_t result[4] = {};
    // leitura por DMA em andamento (perfil de tempo "reios")
    uint32_t xferFad = 0, xferLeft = 0, xferTotal = 0, xferDest = 0;
    uint64_t xferDue = 0;  // ciclo em que o proximo bloco fica pronto
} g;

// Tempo das leituras por DMA (BS_GDROM_TIMING):
//  - instant (padrao): o comando inteiro termina dentro do EXEC_SERVER;
//  - reios: o mesmo modelo do BIOS HLE do Flycast usado no oraculo (so referencia de
//    comportamento, reimplementado): blocos de 5 setores; enquanto restam mais de 5
//    setores, cada bloco leva 1 000 000 ciclos (~2 MB/s); o ultimo, 2 ciclos por byte.
//    O comando fica ocupado ate o ultimo bloco. E o modelo do oraculo, nao o do
//    hardware; serve para as comparacoes deterministicas (STATUS.md, tempo).
int g_timingOverride = -1;  // gdrom_set_timing (testes); -1 = ambiente

bool reiosTiming() {
    if (g_timingOverride >= 0) return g_timingOverride == 1;
    static const bool t = [] {
        const char* e = std::getenv("BS_GDROM_TIMING");
        if (!e || std::strcmp(e, "instant") == 0) return false;
        if (std::strcmp(e, "reios") == 0) return true;
        std::fprintf(stderr, "BS_GDROM_TIMING invalido: %s (use instant ou reios)\n", e);
        std::exit(2);
    }();
    return t;
}

uint64_t reiosTicks(uint32_t sectorsLeft) {
    const uint64_t len = uint64_t(sectorsLeft) * 2048;
    return len > 10240 ? 1000000 : len * 2;
}

std::unique_ptr<DiscSource> g_disc;

enum : uint32_t {
    kCmdPioRead = 0x10, kCmdDmaRead = 0x11, kCmdGetToc2 = 0x13, kCmdInit = 0x18, kCmdReqSes = 0x23,
    kCmdGetVersion = 0x28,
};

// Versao do driver de GD-ROM do BIOS (a mesma que o reios informa; o ultimo byte e 0x02).
void getVersion(Sh4Context* c, uint32_t dest) {
    static const char kVer[] = "GDC Version 1.10 1999-03-31 ";
    for (size_t i = 0; i + 1 < sizeof(kVer); ++i)
        sh4_write8(c, dest + uint32_t(i), i + 2 == sizeof(kVer) ? 0x02 : uint8_t(kVer[i]));
}

// TOC (102 palavras): [0..98] faixas 1..99, [99] primeira, [100] ultima, [101] lead-out;
// cada palavra = ctrl << 28 | adr << 24 | FAD (ou numero da faixa << 16 em [99]/[100]);
// entradas sem faixa = 0xFFFFFFFF. Area 1 (alta densidade) so existe em GD de verdade.
void getToc(Sh4Context* c, uint32_t area, uint32_t dest) {
    uint32_t toc[102];
    for (auto& w : toc) w = 0xFFFFFFFF;
    if (area == 0 && g_disc && !g_disc->tracks().empty()) {
        const auto& tr   = g_disc->tracks();
        auto        info = [](const DiscTrack& t, uint32_t v) { return (t.ctrl << 28) | (1u << 24) | v; };
        for (const auto& t : tr)
            if (t.number >= 1 && t.number <= 99) toc[t.number - 1] = info(t, t.start_fad);
        toc[99]  = info(tr.front(), tr.front().number << 16);
        toc[100] = info(tr.back(), tr.back().number << 16);
        toc[101] = info(tr.back(), tr.back().start_fad + tr.back().length);  // lead-out
    }
    for (int i = 0; i < 102; ++i) sh4_write32(c, dest + 4 * i, toc[i]);
}

bool readSectors(Sh4Context* c, uint32_t fad, uint32_t count, uint32_t dest) {
    uint8_t buf[2048];
    for (uint32_t k = 0; k < count; ++k, dest += 2048) {
        if (!g_disc || !g_disc->read(fad + k, buf)) return false;
        for (uint32_t i = 0; i < 2048; i += 4) {
            uint32_t w;
            std::memcpy(&w, buf + i, 4);
            sh4_write32(c, dest + i, w);
        }
    }
    return true;
}

void execute(Sh4Context* c) {
    static const bool logOn = std::getenv("BS_HLE_LOG") != nullptr;  // diagnostico (ver bios.cpp)
    if (logOn)
        std::fprintf(stderr, "[gdrom] comando %u params %08X %08X %08X %08X\n", g.command, g.params[0], g.params[1],
                     g.params[2], g.params[3]);
    switch (g.command) {
    case kCmdInit:
    case kCmdReqSes:  // o reios tambem nao faz nada aqui
        break;
    case kCmdGetToc2:
        getToc(c, g.params[0], g.params[1]);
        break;
    case kCmdGetVersion:
        getVersion(c, g.params[0]);
        break;
    case kCmdDmaRead:
        if (reiosTiming() && g.params[1]) {  // termina aos blocos (advance)
            g.xferFad   = g.params[0] & 0xFFFFFF;
            g.xferLeft  = g.xferTotal = g.params[1];
            g.xferDest  = g.params[2];
            g.xferDue   = c->cycles + reiosTicks(g.xferLeft);
            g.result[2] = 0;
            return;  // continua ocupado
        }
        [[fallthrough]];
    case kCmdPioRead: {
        const uint32_t fad = g.params[0] & 0xFFFFFF, n = g.params[1], dest = g.params[2];
        if (!readSectors(c, fad, n, dest)) {
            g.status    = kErr;
            g.result[0] = 3;  // erro de midia
            return;
        }
        g.result[2] = n * 2048;
        break;
    }
    default:
        sh4_fatal(c, "comando de GD-ROM ainda nao implementado", c->pc, g.command);
    }
    g.status = kComplete;
}

// Leitura por DMA em blocos (perfil reios): copia os blocos cujo tempo ja passou.
void advance(Sh4Context* c) {
    while (g.xferLeft && c->cycles >= g.xferDue) {
        const uint32_t k = g.xferLeft < 5 ? g.xferLeft : 5;
        if (!readSectors(c, g.xferFad, k, g.xferDest)) {
            g.xferLeft  = 0;
            g.status    = kErr;
            g.result[0] = 3;
            return;
        }
        g.xferFad += k, g.xferDest += k * 2048, g.xferLeft -= k;
        g.result[2] = (g.xferTotal - g.xferLeft) * 2048;
        if (!g.xferLeft) g.status = kComplete;
        else g.xferDue += reiosTicks(g.xferLeft);
    }
}

} // namespace

void gdrom_set_disc(std::unique_ptr<DiscSource> disc) { g_disc = std::move(disc); }
void gdrom_reset() { g = {}; }
void gdrom_set_timing(bool reios) { g_timingOverride = reios ? 1 : 0; }
void gdrom_poll(Sh4Context* c) { advance(c); }

void gdrom_syscall(Sh4Context* c, uint32_t func, uint32_t r4, uint32_t r5) {
    uint32_t& r0 = c->r[0];
    advance(c);  // blocos de leitura cujo tempo ja passou
    switch (func) {
    case kReqCmd:
        if (g.status != kOk) {
            r0 = 0;
            return;
        }
        for (int i = 0; i < 4; ++i) g.params[i] = r5 ? sh4_read32(c, r5 + 4 * i) : 0;
        g.xferLeft = 0;  // pedido novo descarta transferencia antiga (como o reios)
        for (auto& r : g.result) r = 0;
        if (g.nextId == 0 || g.nextId == 0xFFFFFFFF) g.nextId = 1;
        g.lastId  = g.nextId++;
        g.status  = kBusy;
        g.command = r4;
        r0        = g.lastId;
        return;
    case kGetCmdStat: {
        for (int i = 0; i < 4; ++i) sh4_write32(c, r5 + 4 * i, g.result[i]);
        if (g.status == kOk || g.status == kBusy) {
            r0 = uint32_t(g.status);
        } else if (r4 != g.lastId) {
            r0 = kOk;  // nenhum pedido com esse id
        } else {
            r0       = uint32_t(g.status);  // concluido ou erro: informado uma vez
            g.status = kOk;
            g.lastId = 0xFFFFFFFF;
        }
        return;
    }
    case kExecServer:
        if (g.status == kBusy && !g.xferLeft) execute(c);
        return;
    case kInitSystem:
        g = {};
        return;
    case kGetDrvStat:
        sh4_write32(c, r4, kStatPause);
        sh4_write32(c, r4 + 4, kDiscGdrom);
        r0 = kOk;
        return;
    case kReset:
        g.lastId = 0xFFFFFFFF;
        g.status = kOk;
        return;
    default:
        sh4_fatal(c, "funcao GDROM ainda nao implementada", c->pc, func);
    }
}

} // namespace bs
