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
//
// N8.7 4c (docs/N8-7-KAMUI.md): so na validacao. A FIFO de poligonos do legado e a referencia do ListSink, e este
// arquivo, a da entrada da area 4 nativa (native/render/kamui/area4_entry), que roda em sombra: em cada bloco, o
// caminho, o bit do fim de lista e os offsets do caminho de textura sao comparados com os do legado ([area-4]). No
// produto, a entrada e system/area4_bus.cpp.
#include "pvr/ta.h"

#include "cpu/sh4_runtime.h"
#include "pvr/holly_intc.h"
#include "pvr/vram.h"
#include "render/kamui/area4_entry.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace bs {
namespace {

enum : uint32_t { kEndOfList = 0, kUserClip = 1, kObjListSet = 2, kGlobal = 4, kSprite = 5, kVertex = 7 };

// N8.7: a FIFO de poligonos do legado: a referencia do ListSink nativo, que o produto usa
struct State {
    int      list = -1;          // lista aberta (-1: nenhuma)
    bool     vertex64 = false;   // vertices da lista/objeto atual tem 64 bytes
    bool     pendingHalf = false;
    TaParam  half;               // primeira metade de um parametro de 64 bytes
    bool     globalPending = false;  // global de 64 bytes esperando a segunda metade
} g;

std::vector<TaParam> g_params;
uint32_t             g_bytes = 0;  // bytes recebidos pela FIFO de poligonos desde o init
int                  g_lastEnd = -1;  // a lista fechada pelo ultimo bloco (-1: nenhuma)
uint64_t             g_endMismatch = 0;
native::render::kamui::ListSink g_list;  // N8.7: as listas do Kamui, decodificadas direto para a IR

constexpr uint32_t kListEndBit[5] = {1u << 7, 1u << 8, 1u << 9, 1u << 10, 1u << 21};

// N8.7 4c: a entrada da area 4 nativa em sombra
using native::render::kamui::Area4Entry;
uint64_t    g_a4Blocks = 0, g_a4Poly = 0, g_a4Tex = 0, g_a4Ends = 0, g_a4Differ = 0;
std::string g_a4First;

// negativo (BS_AREA4_MUTACAO=banco): a sombra troca o banco da primeira palavra do caminho de textura
bool area4_mutation() {
    static const bool m = [] {
        const char* v = std::getenv("BS_AREA4_MUTACAO");
        return v && std::string(v) == "banco";
    }();
    return m;
}

void area4_differ(uint32_t target, const char* detail) {
    ++g_a4Differ;
    if (g_a4First.empty()) {
        char b[192];
        std::snprintf(b, sizeof b, "bloco %llu em %08X: %s", (unsigned long long)g_a4Blocks, target, detail);
        g_a4First = b;
    }
}

// o caminho que o legado tomou (em ta_fifo_write) contra o da entrada nativa
void area4_route(uint32_t target, Area4Entry::Path legacy) {
    ++g_a4Blocks;
    const Area4Entry::Path n = Area4Entry::route(target);
    if (n != legacy) {
        char b[64];
        std::snprintf(b, sizeof b, "caminho legado %d, nativo %d", int(legacy), int(n));
        area4_differ(target, b);
    }
}

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
        if (g.list >= 0 && g.list <= 4) holly_intc_raise(kListEndBit[g.list]), g_lastEnd = g.list;
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
    g_list.list_init();
    ta_yuv_reset(0, 0);
}

void ta_list_init() {
    g = {};
    g_params.clear();
    g_bytes = 0;
    g_list.list_init();
}

uint32_t                    ta_bytes() { return g_bytes; }
const std::vector<TaParam>& ta_params() { return g_params; }
uint64_t                    ta_list_end_mismatches() { return g_endMismatch; }

std::string ta_area4_summary() {
    return "[area-4] blocos comparados " + std::to_string(g_a4Blocks) + " (poligonos " + std::to_string(g_a4Poly) +
           ", textura " + std::to_string(g_a4Tex) + ", fins de lista " + std::to_string(g_a4Ends) + "), diferentes " +
           std::to_string(g_a4Differ) + (g_a4First.empty() ? std::string() : "; primeira: " + g_a4First) + "\n";
}

void ta_list_continue() {
    g.list = -1;
    g_list.list_continue();
}

native::render::kamui::ListSink& ta_list_sink() { return g_list; }

bool ta_fifo_write(Sh4Context* c, uint32_t target, const uint32_t* data) {
    const uint32_t p = target & 0x1FFFFFFF;
    if ((p >> 26) != 4) {  // area 4: 0x10000000-0x13FFFFFF
        area4_route(target, Area4Entry::Path::Outside);
        return false;
    }
    const uint32_t region = p & 0x01800000;  // 0x00 poligonos, 0x00800000 YUV, 0x01000000+ textura
    if (region == 0) {
        area4_route(target, Area4Entry::Path::Polygon);
        ++g_a4Poly;
        const native::render::kamui::BlockResult r = g_list.feed(data);
        // a FIFO do legado e a referencia (interrupcoes e erro); o fim de lista do ListSink tem de ser o mesmo
        g_bytes += 32;
        g_lastEnd = -1;
        polygonFifo(c, data);
        // negativo (BS_LISTA_NATIVA_MUTACAO=fim): a lista que o ListSink fechou trocada pela seguinte
        static const bool mutEnd = [] {
            const char* v = std::getenv("BS_LISTA_NATIVA_MUTACAO");
            return v && std::string(v) == "fim";
        }();
        const int end = mutEnd && r.list_end >= 0 ? (r.list_end + 1) % 5 : r.list_end;
        if (end != g_lastEnd) ++g_endMismatch;
        // o bit que a entrada nativa acenderia no fim de lista, contra o que a FIFO do legado acendeu
        if (g_lastEnd >= 0) {
            ++g_a4Ends;
            if (Area4Entry::list_end_bit(g_lastEnd) != kListEndBit[g_lastEnd]) {
                char b[96];
                std::snprintf(b, sizeof b, "fim da lista %d: bit legado %08X, nativo %08X", g_lastEnd,
                              kListEndBit[g_lastEnd], Area4Entry::list_end_bit(g_lastEnd));
                area4_differ(target, b);
            }
        }
        return true;
    }
    if (p & 0x01000000) {  // caminho direto de textura: VRAM pelo caminho de 64 bits
        area4_route(target, Area4Entry::Path::Texture);
        ++g_a4Tex;
        const uint32_t off = p & 0x00FFFFE0;
        uint32_t       legacy[8], native[8];
        for (int i = 0; i < 8; ++i) legacy[i] = vram_offset64(off + 4 * i);
        Area4Entry::texture_offsets(target, native);
        if (area4_mutation()) native[0] ^= kVramSize / 2;
        for (int i = 0; i < 8; ++i)
            if (native[i] != legacy[i]) {
                char b[96];
                std::snprintf(b, sizeof b, "palavra %d do caminho de textura: legado %06X, nativo %06X", i, legacy[i],
                              native[i]);
                area4_differ(target, b);
                break;
            }
        for (int i = 0; i < 8; ++i) std::memcpy(vram_data() + legacy[i], &data[i], 4);
        // N8.7 3b: as 8 palavras ficam 4 em cada banco, contiguas (16 bytes em cada)
        vram_touch(legacy[0], 16);
        vram_touch(legacy[1], 16);
        return true;
    }
    area4_route(target, Area4Entry::Path::Yuv);
    ta_yuv_write(c, data);
    return true;
}

[[noreturn]] void ta_yuv_removed(Sh4Context* c, const char* what, uint32_t where) {
    static char msg[224];
    std::snprintf(msg, sizeof msg,
                  "%s (%08X): o filme legado (Sofdec) e o conversor YUV foram removidos do produto no N8.5 (os filmes "
                  "sao nativos; o legado so existe no build de validacao)",
                  what, where);
    std::fprintf(stderr, "[ta] %s\n", msg);
    sh4_fatal(c, msg, c->pc, where);
}

} // namespace bs
