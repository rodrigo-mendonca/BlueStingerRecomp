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
uint32_t             g_bytes = 0;  // bytes recebidos pela FIFO de poligonos desde o init

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
    g_bytes = 0;
    ta_yuv_reset(0, 0);
}

void ta_list_init() {
    g = {};
    g_params.clear();
    g_bytes = 0;
}

uint32_t ta_bytes() { return g_bytes; }

void ta_list_continue() { g.list = -1; }

const std::vector<TaParam>& ta_params() { return g_params; }

bool ta_fifo_write(Sh4Context* c, uint32_t target, const uint32_t* data) {
    const uint32_t p = target & 0x1FFFFFFF;
    if ((p >> 26) != 4) return false;  // area 4: 0x10000000-0x13FFFFFF
    const uint32_t region = p & 0x01800000;  // 0x00 poligonos, 0x00800000 YUV, 0x01000000+ textura
    if (region == 0) {
        g_bytes += 32;
        polygonFifo(c, data);
        return true;
    }
    if (p & 0x01000000) {  // caminho direto de textura: VRAM pelo caminho de 64 bits
        const uint32_t off = p & 0x00FFFFE0;
        for (int i = 0; i < 8; ++i) std::memcpy(vram_data() + vram_offset64(off + 4 * i), &data[i], 4);
        return true;
    }
    ta_yuv_write(c, data);
    return true;
}

// --- conversor YUV (0x10800000): macroblocos YUV420/YUV422 -> textura YUV422 (UYVY) na
// VRAM. Comportamento pela documentacao do PowerVR2 (KallistiOS: TA_YUV_TEX_*); o Flycast so
// como referencia de comportamento.
//   TA_YUV_TEX_BASE (0x005F8148): endereco da textura (caminho de 64 bits).
//   TA_YUV_TEX_CTRL (0x005F814C): bits 5-0 largura em macroblocos - 1 (U), 13-8 altura - 1
//     (V), bit 16: 0 = uma textura de (U+1)*16 x (V+1)*16, 1 = uma textura 16x16 por
//     macrobloco, em sequencia; bit 24: 0 = entrada YUV420, 1 = YUV422.
//   TA_YUV_TEX_CNT (0x005F8150): macroblocos convertidos.
// Macrobloco 420 (384 bytes): U 8x8, V 8x8, Y 4 blocos 8x8 (sup. esq., sup. dir., inf. esq.,
// inf. dir.). 422 (512 bytes): duas metades 16x8 (U 8x8, V 8x8, Y esq. 8x8, Y dir. 8x8). Saida: 2 pixels por palavra de 32
// bits, bytes U, Y0, V, Y1. Fim do quadro (todos os macroblocos): ISTNRM bit 6.
namespace {

struct Yuv {
    uint32_t base = 0, ctrl = 0;
    uint32_t count = 0;  // macroblocos convertidos neste quadro
    uint32_t fill  = 0;  // bytes do macrobloco atual
    uint8_t  mb[512]{};
} g_yuv;

void yuvConvert() {
    const bool     yuv422 = (g_yuv.ctrl >> 24) & 1;
    const bool     tiles  = (g_yuv.ctrl >> 16) & 1;
    const uint32_t wMb = (g_yuv.ctrl & 0x3F) + 1, hMb = ((g_yuv.ctrl >> 8) & 0x3F) + 1;
    const uint32_t mx = g_yuv.count % wMb, my = g_yuv.count / wMb;
    for (uint32_t row = 0; row < 16; ++row) {
        // 420: U 8x8, V 8x8, Y 4 blocos 8x8 (sup. esq., sup. dir., inf. esq., inf. dir.).
        // 422: duas metades 16x8, cada uma U 8x8, V 8x8, Y 2 blocos 8x8 (esq., dir.) (revisao
        // Codex 2026-10-15; ordem do exemplo yuv422 do KallistiOS).
        const uint8_t* half = g_yuv.mb + (yuv422 ? (row / 8) * 256 : 0);
        const uint8_t* u    = half;
        const uint8_t* v    = half + 64;
        const uint8_t* y    = half + 128;
        for (uint32_t col = 0; col < 16; col += 2) {
            auto Y = [&](uint32_t x) {
                const uint32_t block = yuv422 ? x / 8 : (row / 8) * 2 + x / 8;
                return y[block * 64 + (row % 8) * 8 + x % 8];
            };
            const uint32_t ci = (yuv422 ? row % 8 : row / 2) * 8 + col / 2;
            const uint32_t word = uint32_t(u[ci]) | uint32_t(Y(col)) << 8 | uint32_t(v[ci]) << 16 |
                                  uint32_t(Y(col + 1)) << 24;
            const uint32_t off = tiles ? g_yuv.count * 512 + (row * 16 + col) * 2
                                       : ((my * 16 + row) * wMb * 16 + mx * 16 + col) * 2;
            std::memcpy(vram_data() + vram_offset64(g_yuv.base + off), &word, 4);
        }
    }
    if (++g_yuv.count >= wMb * hMb) {
        g_yuv.count = 0;
        holly_intc_raise(1u << 6);  // fim da transferencia YUV
    }
}

} // namespace

void ta_yuv_reset(uint32_t base, uint32_t ctrl) {
    static const bool debug = std::getenv("BS_YUV_DEBUG") != nullptr;  // diagnostico
    if (debug && (base || ctrl)) std::fprintf(stderr, "[yuv] base %08X ctrl %08X\n", base, ctrl);
    g_yuv      = {};
    g_yuv.base = base & 0x00FFFFF8;
    g_yuv.ctrl = ctrl;
}

uint32_t ta_yuv_count() { return g_yuv.count; }

void ta_yuv_write(Sh4Context*, const uint32_t* data) {
    const uint32_t size = ((g_yuv.ctrl >> 24) & 1) ? 512 : 384;
    std::memcpy(g_yuv.mb + g_yuv.fill, data, 32);
    g_yuv.fill += 32;
    if (g_yuv.fill >= size) {
        // diagnostico: BS_YUV_DUMP=<arquivo> grava os macroblocos de entrada (ate 2000)
        static const char* dumpPath = std::getenv("BS_YUV_DUMP");
        static FILE*       dump     = dumpPath ? std::fopen(dumpPath, "wb") : nullptr;
        static int         dumped   = 0;
        if (dump && dumped < 2000) {
            std::fwrite(g_yuv.mb, 1, size, dump);
            if (++dumped == 2000) std::fclose(dump);
        }
        yuvConvert();
        g_yuv.fill = 0;
    }
}

} // namespace bs
