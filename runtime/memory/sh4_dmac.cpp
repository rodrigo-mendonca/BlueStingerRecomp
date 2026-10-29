// DMAC (manual SH7750, cap. 14): por canal n, SARn 0xFFA00000+0x10n, DARn +4,
// DMATCRn +8, CHCRn +0xC; DMAOR 0xFFA00040. CHCR: bit 0 DE (habilita), bit 1 TE (fim).
// DMAOR bit 0 DME (habilita o controlador). O canal 2 em modo DDT e alimentado pelo
// Holly: o boot do jogo grava SAR2 (origem na RAM) e CHCR2, e dispara por SB_C2DST.
//
// N8.7 4b (docs/N8-7-KAMUI.md): so na validacao, como referencia da copia do canal 2 nativa
// (native/render/kamui/channel_dma), que roda em sombra: recebe as mesmas escritas e, em cada
// disparo, copia antes do legado para um destino que so registra os blocos. Depois de cada
// operacao a comparacao confere os registradores do DMAC, os do system block, o resultado e a
// sequencia de blocos (destino e conteudo).
#include "memory/sh4_dmac.h"

#include "cpu/sh4_mem.h"
#include "debug/gfx_probe.h"
#include "cpu/sh4_runtime.h"
#include "pvr/ta.h"
#include "render/kamui/channel_dma.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bs {
namespace {

constexpr uint32_t kBase = 0xFFA00000, kDmaor = 0xFFA00040;
constexpr uint32_t kDe = 1u << 0, kTe = 1u << 1, kDme = 1u << 0;

uint32_t g_ch[4][4] = {};  // SAR, DAR, DMATCR, CHCR
uint32_t g_dmaor    = 0;

bool chanReg(uint32_t a, uint32_t*& r) {
    if (a < kBase || a >= kBase + 0x40 || (a & 3)) return false;
    r = &g_ch[(a - kBase) >> 4][((a - kBase) & 0xF) >> 2];
    return true;
}

bool ch2_transfer(Sh4Context* c, uint32_t dest, uint32_t len);

// a sombra: FNV-1a dos blocos (destino e as 8 palavras), na ordem
using native::render::kamui::ChannelDma;
ChannelDma  g_shadow;
uint64_t    g_checks = 0, g_differ = 0, g_blocks = 0;
std::string g_first;

struct Trail {
    uint64_t hash = 1469598103934665603ull, blocks = 0;
    void     add(uint32_t dest, const uint32_t* blk) {
        auto mix = [this](uint32_t w) {
            for (int i = 0; i < 4; ++i) hash = (hash ^ ((w >> (8 * i)) & 0xFF)) * 1099511628211ull;
        };
        mix(dest);
        for (int i = 0; i < 8; ++i) mix(blk[i]);
        ++blocks;
    }
};
Trail* g_legacyTrail = nullptr;  // os blocos que o legado entrega no disparo em andamento

// negativo (BS_DMA_CANAL2_MUTACAO=origem): a sombra le a origem um bloco adiante (a comparacao tem de acusar)
bool mutation_source() {
    static const bool m = [] {
        const char* v = std::getenv("BS_DMA_CANAL2_MUTACAO");
        return v && std::string(v) == "origem";
    }();
    return m;
}

// leitura da sombra direto da RAM guest (sem os ganchos de leitura: o legado ja le pelo caminho de sempre); fora da RAM,
// a palavra vale 0 e a sequencia acusa a diferenca
uint32_t shadow_read(void* ctx, uint32_t a) {
    Sh4Context* c = static_cast<Sh4Context*>(ctx);
    if (mutation_source()) a += 32;
    if (!sh4_is_ram(a) || (a & 3)) return 0;
    uint32_t v;
    std::memcpy(&v, c->ram + (a & kRamMask), 4);
    return v;
}

Trail* g_shadowTrail = nullptr;
bool   shadow_sink(void*, uint32_t dest, const uint32_t* blk) {
    g_shadowTrail->add(dest, blk);
    return true;
}

void differ(const char* what, const char* detail) {
    ++g_differ;
    if (g_first.empty()) {
        char b[224];
        std::snprintf(b, sizeof b, "%s (operacao %llu): %s", what, (unsigned long long)g_checks, detail);
        g_first = b;
    }
}

void check_regs(const char* what) {
    ++g_checks;
    char b[96];
    if (g_shadow.reg(kDmaor) != g_dmaor) {
        std::snprintf(b, sizeof b, "DMAOR legado %08X, nativo %08X", g_dmaor, g_shadow.reg(kDmaor));
        return differ(what, b);
    }
    for (uint32_t n = 0; n < 4; ++n)
        for (uint32_t r = 0; r < 4; ++r)
            if (g_shadow.reg(kBase + 0x10 * n + 4 * r) != g_ch[n][r]) {
                std::snprintf(b, sizeof b, "canal %u registrador +%X legado %08X, nativo %08X", n, 4 * r, g_ch[n][r],
                              g_shadow.reg(kBase + 0x10 * n + 4 * r));
                return differ(what, b);
            }
}

} // namespace

void sh4_dmac_reset() {
    for (auto& ch : g_ch)
        for (auto& r : ch) r = 0;
    g_dmaor = 0;
    g_shadow.reset();
}

bool sh4_dmac_read(uint32_t a, unsigned size, uint32_t& v) {
    if (size != 4) return false;
    uint32_t* r = nullptr;
    if (a == kDmaor) v = g_dmaor;
    else if (chanReg(a, r)) v = *r;
    else return false;
    return true;
}

bool sh4_dmac_write(uint32_t a, unsigned size, uint32_t v) {
    if (size != 4) return false;
    uint32_t* r = nullptr;
    if (a == kDmaor) g_dmaor = v;
    else if (chanReg(a, r)) *r = v;
    else return false;
    g_shadow.write(a, v);
    check_regs("escrita");
    return true;
}

bool sh4_dmac_ch2_start(Sh4Context* c, uint32_t& dest, uint32_t& len, uint32_t& c2dst) {
    // a sombra primeiro, sobre a memoria que o legado vai ler
    ChannelDma::SbRegs sb{dest, len, c2dst};
    Trail              shadowTrail, legacyTrail;
    g_shadowTrail          = &shadowTrail;
    const bool shadowDone  = g_shadow.ch2_start(sb, shadow_read, shadow_sink, c) == ChannelDma::Result::Done;
    g_legacyTrail          = &legacyTrail;
    const bool done        = ch2_transfer(c, dest, len);
    g_legacyTrail          = nullptr;
    // o system block, como fazia o holly_sb.cpp
    if (done) {
        dest += len;
        len   = 0;
        c2dst = 0;
    }
    check_regs("disparo do canal 2");
    char b[160];
    g_blocks += legacyTrail.blocks;
    if (shadowDone != done) {
        std::snprintf(b, sizeof b, "copia feita: legado %d, nativo %d", int(done), int(shadowDone));
        differ("disparo do canal 2", b);
    } else if (shadowTrail.hash != legacyTrail.hash || shadowTrail.blocks != legacyTrail.blocks) {
        std::snprintf(b, sizeof b, "blocos: legado %llu (%016llX), nativo %llu (%016llX)",
                      (unsigned long long)legacyTrail.blocks, (unsigned long long)legacyTrail.hash,
                      (unsigned long long)shadowTrail.blocks, (unsigned long long)shadowTrail.hash);
        differ("disparo do canal 2", b);
    } else if (sb.c2dstat != dest || sb.c2dlen != len || sb.c2dst != c2dst) {
        std::snprintf(b, sizeof b, "system block: legado %08X/%08X/%08X, nativo %08X/%08X/%08X", dest, len, c2dst,
                      sb.c2dstat, sb.c2dlen, sb.c2dst);
        differ("disparo do canal 2", b);
    }
    return done;
}

std::string sh4_dmac_ch2_summary() {
    return "[canal-2] operacoes comparadas " + std::to_string(g_checks) + ", copias " +
           std::to_string(g_shadow.transfers()) + " (" + std::to_string(g_blocks) + " blocos), diferentes " +
           std::to_string(g_differ) + (g_first.empty() ? std::string() : "; primeira: " + g_first) + "\n";
}

namespace {

bool ch2_transfer(Sh4Context* c, uint32_t dest, uint32_t len) {
    uint32_t* ch = g_ch[2];
    if (!(g_dmaor & kDme) || !(ch[3] & kDe)) return false;
    const bool fifo = (dest & 0x01800000) == 0;  // FIFO de poligonos: o endereco nao avanca
    uint32_t   src  = ch[0];
    if (g_gfx_probe) gfx_probe_dma(c, src, dest, len);
    for (uint32_t off = 0; off < len; off += 32, src += 32) {
        uint32_t blk[8];
        for (int i = 0; i < 8; ++i) blk[i] = sh4_read32(c, src + 4 * i);
        if (g_legacyTrail) g_legacyTrail->add(fifo ? dest : dest + off, blk);
        if (g_gfx_probe) gfx_probe_ta(c, fifo ? dest : dest + off, GfxPath::Dma);
        if (!ta_fifo_write(c, fifo ? dest : dest + off, blk))
            sh4_fatal(c, "DMA do canal 2 para destino fora da area 4", c->pc, dest);
    }
    ch[0] = src;
    ch[2] = 0;
    ch[3] |= kTe;
    return true;
}

} // namespace

} // namespace bs
