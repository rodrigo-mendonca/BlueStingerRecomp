// Barramento Maple (documentacao publica do barramento: Marcus Comstedt, "Maple bus";
// KallistiOS maple_dma.c como referencia de comportamento; nada copiado do Flycast).
//
// Registradores: SB_MDSTAR 0x005F6C04 (tabela, endereco fisico), SB_MDTSEL 0x005F6C10
// (disparo: 0 software, 1 VBlank), SB_MDEN 0x005F6C14 (habilita), SB_MDST 0x005F6C18
// (1 inicia; le 1 enquanto o DMA corre). Os demais guardam o valor escrito.
//
// Tabela: por porta,
//   palavra 0: bit 31 ultima entrada, bits 17-16 porta, bits 10-8 padrao (0 normal),
//              bits 7-0 palavras de dados do quadro
//   palavra 1: endereco fisico do buffer de recepcao
//   quadro: cabecalho (comando | destino << 8 | origem << 16 | palavras << 24) + dados
// Resposta no buffer: o mesmo formato de quadro; sem dispositivo, 0xFFFFFFFF.
// Enderecos: porta p, host p << 6; dispositivo principal (p << 6) | 0x20 (na resposta,
// mais um bit por unidade de expansao presente); unidade n: (p << 6) | (1 << n).
//
// Dados do controle (conferidos com as respostas que o jogo recebe no Flycast,
// checkpoint 8C1C4914): funcao 0x01000000 (controle), dados da funcao 0xFE060F00,
// area 0xFF, conector 0, "Dreamcast Controller", licenca da SEGA, consumo 0x01AE/0x01F4.
// Condicao: botoes (16 bits, ativo em 0), gatilhos R/L, analogicos (0x80 = centro).
//
// Tempo: o barramento roda a 2 Mbit/s; o fim do DMA e agendado para 100 ciclos do
// SH-4 (200 MHz) por bit enviado ou recebido. Aproximacao (registrada no STATUS.md).
#include "maple/maple.h"
#include "cpu/time_hooks.h"
#include "debug/clock_log.h"
#include "debug/intc_trace.h"

#include "cpu/sh4_runtime.h"
#include "input/input.h"
#include "pvr/holly_intc.h"
#include "system/graphics_ports.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bs {
namespace {

constexpr uint32_t kMdstar = 0x005F6C04, kMdtsel = 0x005F6C10, kMden = 0x005F6C14, kMdst = 0x005F6C18;
constexpr uint32_t kBase = 0x005F6C00, kSize = 0x100;

constexpr uint8_t kCmdDevInfo = 0x01, kCmdAllStatus = 0x02, kCmdReset = 0x03, kCmdShutdown = 0x04,
                  kCmdGetCond = 0x09;
constexpr uint8_t kRspDevInfo = 0x05, kRspAllStatus = 0x06, kRspAck = 0x07, kRspData = 0x08,
                  kRspFuncUnsupported = 0xFE, kRspUnknownCmd = 0xFD;
constexpr uint32_t kFuncController = 0x01000000;

std::array<uint32_t, kSize / 4> g_regs{};
uint64_t                        g_due = 0;
MapleControllerState            g_pad;
uint64_t                        g_condSeq[4] = {};  // pedidos de condicao por porta (roteiro)

// Diagnosticos e negativos do N3-0 (desligados por padrao; nenhum muda o jogo sem a variavel):
//   BS_MAPLE_FUNCAO=<hex>  dados da funcao do controle no DEVICE_INFO (cobertura do `support`)
//   BS_PAD_LEITURAS=<arq>  cada GET_CONDITION da porta A: leitura, passo, VBlank, estado servido
//   BS_PAD_MUTACAO=troca_ab           A e B trocados no estado servido
//   BS_PAD_MUTACAO=amostra_vblank_in  a amostra vem da entrega do VBlank-In (maple_vblank_in),
//                                     nao do disparo do DMA
struct PadDiag {
    bool     funcSet = false;
    uint32_t func    = 0xFE060F00;
    FILE*    reads   = nullptr;
    bool     swapAB = false, sampleAtVblank = false;
};
const PadDiag& padDiag() {
    static const PadDiag d = [] {
        PadDiag x;
        if (const char* v = std::getenv("BS_MAPLE_FUNCAO"); v && *v) x.funcSet = true, x.func = uint32_t(std::strtoul(v, nullptr, 16));
        if (const char* v = std::getenv("BS_PAD_LEITURAS"); v && *v) {
            x.reads = std::fopen(v, "w");
            if (x.reads) std::fprintf(x.reads, "leitura\tpasso\tvblank\testado\n");
        }
        if (const char* v = std::getenv("BS_PAD_MUTACAO"); v && *v) {
            x.swapAB         = std::strstr(v, "troca_ab") != nullptr;
            x.sampleAtVblank = std::strstr(v, "amostra_vblank_in") != nullptr;
        }
        return x;
    }();
    return d;
}
bool          g_controllerPresent = true;  // N3-2: false no --controles nativo
MapleCounters g_counters;
PadState g_vblankSample;          // amostra_vblank_in: a amostra guardada na entrega do VBlank-In
bool     g_transitionArmed = false;  // o primeiro acesso ao MDST depois do VBlank-In avanca a transicao

uint32_t& reg(uint32_t p) { return g_regs[(p - kBase) / 4]; }

uint8_t* ramAt(Sh4Context* c, uint32_t phys, uint32_t bytes) {
    phys &= 0x1FFFFFFF;
    if ((phys >> 26) != 3 || (phys & 0x00FFFFFF) + bytes > (16u << 20))
        sh4_fatal(c, "DMA do Maple fora da RAM", c->pc, phys);
    return c->ram + (phys & 0x00FFFFFF);
}

uint32_t rd(Sh4Context* c, uint32_t phys) {
    uint32_t v;
    std::memcpy(&v, ramAt(c, phys, 4), 4);
    return v;
}

void putStr(uint8_t* dst, const char* s, size_t n) {
    std::memset(dst, ' ', n);
    std::memcpy(dst, s, std::strlen(s) < n ? std::strlen(s) : n);
}

// Informacao do dispositivo (28 palavras).
void devInfo(uint32_t* w) {
    std::memset(w, 0, 28 * 4);
    w[0]        = kFuncController;
    w[1]        = padDiag().func;
    auto* b     = reinterpret_cast<uint8_t*>(w);
    b[16]       = 0xFF;  // area
    b[17]       = 0;     // direcao do conector
    putStr(b + 18, "Dreamcast Controller", 30);
    putStr(b + 48, "Produced By or Under License From SEGA ENTERPRISES,LTD.", 60);
    const uint16_t standby = 0x01AE, maxPower = 0x01F4;
    std::memcpy(b + 108, &standby, 2);
    std::memcpy(b + 110, &maxPower, 2);
}

// Resposta do dispositivo para um quadro; devolve as palavras da resposta (com o
// cabecalho) gravadas em `out`, ou 0 se ninguem responde.
uint32_t respond(uint32_t port, uint32_t hdr, const uint32_t* data, uint32_t* out) {
    const uint8_t cmd = hdr & 0xFF, dst = (hdr >> 8) & 0xFF, src = (hdr >> 16) & 0xFF;
    // dispositivo principal so no endereco da propria porta (revisao Codex 2026-10-14)
    const bool    main = dst == ((port << 6) | 0x20);
    // BS_MAPLE_PORTS=none: nenhum dispositivo conectado (configuracao real do hardware)
    static const bool none = [] {
        const char* e = std::getenv("BS_MAPLE_PORTS");
        return e && std::strcmp(e, "none") == 0;
    }();
    (cmd == kCmdDevInfo ? g_counters.devInfo : cmd == kCmdGetCond ? g_counters.getCond : g_counters.other)[port & 3]++;
    // so o controle da porta A existe; no --controles nativo (N3-2), nem ele
    if (none || !g_controllerPresent || port != 0 || !main) return 0;
    // Fonte de entrada do host/roteiro (runtime/input), se houver. N3-0: um estado
    // "DESCONECTADO" deixa a porta vazia para todos os comandos (sondagem de conexao do SDK).
    // N3-1: a amostra (latch) so no GET_CONDITION; os outros comandos so perguntam a presenca.
    InputSource* in = input_source();
    PadState     s;
    if (in) {
        if (padDiag().sampleAtVblank) s = g_vblankSample;
        else if (cmd == kCmdGetCond) s = in->read(int(port), g_condSeq[port]), ++g_counters.sourceReads;
        else s.connected = in->connected(int(port), g_condSeq[port]), ++g_counters.sourceReads;
    }
    if (!s.connected) return 0;
    ++g_counters.controllerResponses;
    const uint8_t me = uint8_t(port << 6) | 0x20;  // sem unidades de expansao
    auto frame = [&](uint8_t rcmd, uint32_t words) {
        out[0] = rcmd | (uint32_t(src) << 8) | (uint32_t(me) << 16) | (words << 24);
        return words + 1;
    };
    switch (cmd) {
    case kCmdDevInfo:
        devInfo(out + 1);
        return frame(kRspDevInfo, 28);
    case kCmdAllStatus:
        devInfo(out + 1);
        std::memset(out + 29, 0, 20 * 4);  // area livre: sem dados
        return frame(kRspAllStatus, 48);
    case kCmdReset:
    case kCmdShutdown:
        return frame(kRspAck, 0);
    case kCmdGetCond: {
        if (data[0] != kFuncController) return frame(kRspFuncUnsupported, 0);
        // A fonte de entrada (lida acima, na leitura corrente); sem fonte, o estado fixo de
        // maple_set_controller. O barramento e ativo em 0; a entrada, em 1.
        MapleControllerState pad = g_pad;
        if (in) {
            const PadDiag& d = padDiag();
            if (d.reads)
                std::fprintf(d.reads, "%llu\t%lld\t%llu\t%s\n", static_cast<unsigned long long>(g_condSeq[port]),
                             static_cast<long long>(input_step_or(~0ull)), static_cast<unsigned long long>(input_transition_index()),
                             pad_state_text(s).c_str());
            ++g_condSeq[port];
            uint16_t b = s.buttons;
            if (d.swapAB) b = uint16_t((b & ~(kPadA | kPadB)) | (b & kPadA ? kPadB : 0) | (b & kPadB ? kPadA : 0));
            pad.buttons = uint16_t(~b);
            pad.ltrig = s.ltrig, pad.rtrig = s.rtrig, pad.joyx = s.joyx, pad.joyy = s.joyy;
            pad.joy2x = s.joy2x, pad.joy2y = s.joy2y;
        }
        out[1] = kFuncController;
        out[2] = pad.buttons | (uint32_t(pad.rtrig) << 16) | (uint32_t(pad.ltrig) << 24);
        out[3] = pad.joyx | (uint32_t(pad.joyy) << 8) | (uint32_t(pad.joy2x) << 16) | (uint32_t(pad.joy2y) << 24);
        return frame(kRspData, 3);
    }
    default: {
        static bool seen[256];
        if (!seen[cmd]) {
            seen[cmd] = true;
            std::fprintf(stderr, "[maple] comando %02X sem modelo: resposta 'comando desconhecido'\n", cmd);
        }
        return frame(kRspUnknownCmd, 0);
    }
    }
}

void runDma(Sh4Context* c) {
    static const bool debug = std::getenv("BS_MAPLE_DEBUG") != nullptr;
    uint32_t          addr  = reg(kMdstar) & 0x1FFFFFE0;
    uint64_t          bits  = 0;
    ++g_counters.dmas;
    // segue os descritores ate o bit LAST (revisao Codex 2026-10-14); sem LAST em 1024
    // descritores, a tabela esta corrompida: erro fatal, nunca resposta parcial em silencio
    for (int n = 0;; ++n) {
        if (n == 1024) sh4_fatal(c, "Maple: tabela sem descritor final (LAST)", c->pc, reg(kMdstar));
        const uint32_t instr = rd(c, addr), recv = rd(c, addr + 4);
        const uint32_t port = (instr >> 16) & 3, pattern = (instr >> 8) & 7, len = instr & 0xFF;
        if (pattern != 0) sh4_fatal(c, "Maple: padrao de transferencia sem modelo", c->pc, instr);
        const uint32_t hdr = rd(c, addr + 8);
        uint32_t       data[256], out[1 + 256];
        for (uint32_t i = 0; i < len; ++i) data[i] = rd(c, addr + 12 + 4 * i);
        const uint32_t words = respond(port, hdr, data, out);
        uint8_t*       dst   = ramAt(c, recv, words ? words * 4 : 4);
        if (words) std::memcpy(dst, out, words * 4);
        else std::memset(dst, 0xFF, 4);  // sem resposta
        bits += (1 + len + (words ? words : 1)) * 32ull;
        if (debug)
            std::fprintf(stderr, "[maple] porta %u cmd %02X dst %02X -> buffer %08X: %u palavras (%08X)\n", port,
                         hdr & 0xFF, (hdr >> 8) & 0xFF, recv, words, words ? out[0] : 0xFFFFFFFFu);
        addr += 12 + 4 * len;
        if (instr & 0x80000000) break;
    }
    g_due = time_now(c) + bits * 100;
    if (g_due == 0) g_due = 1;
    time_provider().maple_scheduled(g_due);
}

} // namespace

void maple_reset() {
    g_regs.fill(0);
    g_due = 0;
    time_provider().maple_cancel();
    g_pad = {};
    for (auto& n : g_condSeq) n = 0;
    g_vblankSample    = {};
    g_transitionArmed = false;
    g_counters        = {};
}

void maple_set_controller(const MapleControllerState& s) { g_pad = s; }

bool maple_read(Sh4Context* c, uint32_t a, uint32_t& v) {
    const uint32_t p = a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0;
    if (p < kBase || p >= kBase + kSize) return false;
    v = reg(p);
    if (p == kMdst) {
        if (g_transitionArmed) g_transitionArmed = false, input_transition_tick();  // N3-0: transicao injetada
        v = (g_due && time_now(c) < g_due) ? 1 : 0;
        if (g_clock_log) clock_log_read(ClockRead::Mdst, v);
        if (g_intc_trace) intc_trace_clock(IntcSource::Mdst, v);
    }
    return true;
}

bool maple_write(Sh4Context* c, uint32_t a, uint32_t v) {
    const uint32_t p = a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0;
    if (p < kBase || p >= kBase + kSize) return false;
    if (p == kMdtsel && (v & 1)) {
        static bool warned = false;
        if (!warned) std::fprintf(stderr, "[maple] disparo por VBlank (MDTSEL=1) ainda sem modelo\n");
        warned = true;
    }
    if (p == kMdst) {
        if (g_due && time_now(c) >= g_due) {  // fim anterior ainda nao visto pelo controlador
            event_status_raise(kIstMapleDma);
            g_due = 0;
            time_provider().maple_cancel();
        }
        if ((v & 1) && (reg(kMden) & 1) && !g_due) runDma(c);
        return true;
    }
    reg(p) = v;
    return true;
}

void maple_vblank_in() {
    // amostra_vblank_in (negativo do N3-0): guarda a amostra na entrega do VBlank-In. A transicao
    // dos roteiros "indice transicao" so acontece depois, no primeiro acesso ao MDST (maple_read)
    if (padDiag().sampleAtVblank && g_controllerPresent)
        if (InputSource* in = input_source()) g_vblankSample = in->read(0, g_condSeq[0]);
    g_transitionArmed = true;
}
void          maple_set_controller_present(bool present) { g_controllerPresent = present; }
bool          maple_controller_present() { return g_controllerPresent; }
MapleCounters maple_counters() { return g_counters; }
void          maple_counters_reset() { g_counters = {}; }
uint64_t maple_dma_due() { return g_due; }
uint64_t maple_cond_seq(int port) { return port >= 0 && port < 4 ? g_condSeq[port] : 0; }

void maple_dma_take() { g_due = 0; }

const PeriphBackend* maple_bus_backend() {
    static const PeriphBackend b{maple_reset, maple_read, maple_write, maple_vblank_in, maple_dma_take, maple_counters,
                                 maple_cond_seq};
    return &b;
}

} // namespace bs
