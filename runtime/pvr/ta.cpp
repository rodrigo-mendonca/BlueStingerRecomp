// TA (documentacao de hardware do PowerVR2 via KallistiOS; Flycast so como referencia de
// comportamento). Palavra de controle de parametro (PCW):
//   bits 31-29 tipo: 0 fim de lista, 1 recorte de tile, 2 conjunto de lista de objetos,
//              4 global de poligono/modificador, 5 global de sprite, 7 vertice
//   bit 28 fim de strip; bits 26-24 tipo de lista; bits 7-0 controle do objeto:
//   bit 0 UV 16 bits, 2 offset, 3 textura, 5-4 tipo de cor, 6 dois volumes
// Tamanhos: global de poligono tipo 2 (textura + intensidade + offset) e tipo 4 (dois
// volumes + intensidade) tem 64 bytes; os demais globais, 32. Vertices de 64 bytes:
// tipos 5/6 (textura + cor float), 11-14 (textura + dois volumes), sprites e
// volumes modificadores. Fim de lista acende ISTNRM: opaca bit 7, modificador opaco 8,
// translucida 9, modificador translucido 10, punch-through 21.
#include "pvr/ta.h"

#include "cpu/sh4_runtime.h"
#include "pvr/holly_intc.h"
#include "pvr/vram.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bs {
namespace {

enum : uint32_t { kEndOfList = 0, kUserClip = 1, kObjListSet = 2, kGlobal = 4, kSprite = 5, kVertex = 7 };

struct State {
    int      list = -1;          // lista aberta (-1: nenhuma)
    bool     vertex64 = false;   // vertices da lista/objeto atual tem 64 bytes
    bool     pendingHalf = false;
    TaParam  half;               // primeira metade de um parametro de 64 bytes
    bool     globalPending = false;  // global de 64 bytes esperando a segunda metade
} g;

std::vector<TaParam> g_params;

constexpr uint32_t kListEndBit[5] = {1u << 7, 1u << 8, 1u << 9, 1u << 10, 1u << 21};

bool polyGlobal64(uint32_t pcw) {
    const uint32_t col = (pcw >> 4) & 3, tex = (pcw >> 3) & 1, off = (pcw >> 2) & 1, vol = (pcw >> 6) & 1;
    return vol ? col == 2 : (col == 2 && tex && off);
}

bool polyVertex64(uint32_t pcw) {
    const uint32_t col = (pcw >> 4) & 3, tex = (pcw >> 3) & 1, vol = (pcw >> 6) & 1;
    if (!tex) return false;
    return vol ? true : col == 1;  // textura + dois volumes: 11-14; textura + cor float: 5/6
}

void push(const uint32_t* w, uint32_t words) {
    TaParam p;
    p.list  = uint32_t(g.list < 0 ? 0 : g.list);
    p.words = words;
    std::memcpy(p.w, w, words * 4);
    g_params.push_back(p);
}

void logParam(Sh4Context* c, uint32_t pcw) {
    // Diagnostico: BS_TA_LOG=<n> registra os n primeiros parametros (padrao 0)
    static const char* env   = std::getenv("BS_TA_LOG");
    static const int   limit = env ? std::atoi(env) : 0;
    static int         n     = 0;
    if (n++ < limit) std::fprintf(stderr, "[ta] pr=%08X pcw=%08X tipo=%u lista=%d\n", c->pr, pcw, pcw >> 29, g.list);
}

void polygonFifo(Sh4Context* c, const uint32_t* d) {
    if (g.pendingHalf) {  // segunda metade de um parametro de 64 bytes
        uint32_t w[16];
        std::memcpy(w, g.half.w, 32);
        std::memcpy(w + 8, d, 32);
        push(w, 16);
        g.pendingHalf = false;
        return;
    }
    const uint32_t pcw  = d[0];
    const uint32_t type = pcw >> 29;
    logParam(c, pcw);
    if ((type == kGlobal || type == kSprite) && g.list < 0) g.list = int((pcw >> 24) & 7);
    switch (type) {
    case kEndOfList:
        if (g.list >= 0 && g.list <= 4) holly_intc_raise(kListEndBit[g.list]);
        push(d, 8);
        g.list = -1;
        return;
    case kUserClip:
    case kObjListSet:
        push(d, 8);
        return;
    case kGlobal: {
        const bool modvol = g.list == 1 || g.list == 3;
        g.vertex64        = modvol || polyVertex64(pcw);
        if (!modvol && polyGlobal64(pcw)) break;  // 64 bytes: espera a segunda metade
        push(d, 8);
        return;
    }
    case kSprite:
        g.vertex64 = true;
        push(d, 8);
        return;
    case kVertex:
        if (g.vertex64) break;
        push(d, 8);
        return;
    default:
        sh4_fatal(c, "parametro do TA com tipo invalido", c->pc, pcw);
    }
    std::memcpy(g.half.w, d, 32);
    g.pendingHalf = true;
}

} // namespace

void ta_reset() {
    g = {};
    g_params.clear();
}

void ta_list_init() {
    g = {};
    g_params.clear();
}

void ta_list_continue() { g.list = -1; }

const std::vector<TaParam>& ta_params() { return g_params; }

bool ta_fifo_write(Sh4Context* c, uint32_t target, const uint32_t* data) {
    const uint32_t p = target & 0x1FFFFFFF;
    if ((p >> 26) != 4) return false;  // area 4: 0x10000000-0x13FFFFFF
    const uint32_t region = p & 0x01800000;  // 0x00 poligonos, 0x00800000 YUV, 0x01000000+ textura
    if (region == 0) {
        polygonFifo(c, data);
        return true;
    }
    if (p & 0x01000000) {  // caminho direto de textura: VRAM pelo caminho de 64 bits
        const uint32_t off = p & 0x00FFFFE0;
        for (int i = 0; i < 8; ++i) std::memcpy(vram_data() + vram_offset64(off + 4 * i), &data[i], 4);
        return true;
    }
    sh4_fatal(c, "conversor YUV do TA ainda nao implementado", c->pc, target);
}

} // namespace bs
