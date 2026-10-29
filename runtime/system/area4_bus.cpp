// N8.7 (docs/N8-7-KAMUI.md, incremento 4c): no produto, a entrada da area 4 (pvr/ta.h) e a nativa
// (native/render/kamui/area4_entry): o caminho de cada bloco das store queues e da copia do canal 2, as listas do Kamui
// para o ListSink (o fim de lista acende ISTNRM no banco de status, no mesmo bloco), o caminho de textura na memoria das
// texturas (vram_data/vram_touch, a TextureHeap do 3b) e o erro explicito do conversor YUV removido (N8.5). O TA do
// legado (pvr/ta.cpp, com a FIFO de poligonos de referencia) fica na validacao.
#include "pvr/ta.h"

#include "cpu/sh4_runtime.h"
#include "pvr/holly_intc.h"
#include "pvr/vram.h"
#include "render/kamui/area4_entry.h"

#include <cstdio>
#include <cstring>

namespace bs {
namespace {

using native::render::kamui::Area4Entry;

native::render::kamui::ListSink g_list;  // as listas do Kamui, decodificadas direto para a IR

} // namespace

void ta_reset() { g_list.list_init(); }
void ta_list_init() { g_list.list_init(); }
void ta_list_continue() { g_list.list_continue(); }

uint32_t                         ta_bytes() { return g_list.bytes(); }
const std::vector<TaParam>&      ta_params() { return g_list.params(); }
uint64_t                         ta_list_end_mismatches() { return 0; }  // a comparacao com a FIFO do legado: validacao
native::render::kamui::ListSink& ta_list_sink() { return g_list; }
std::string                      ta_area4_summary() { return {}; }

bool ta_fifo_write(Sh4Context* c, uint32_t target, const uint32_t* data) {
    switch (Area4Entry::route(target)) {
    case Area4Entry::Path::Outside: return false;
    case Area4Entry::Path::Polygon: {
        const native::render::kamui::BlockResult r = g_list.feed(data);
        if (r.invalid) sh4_fatal(c, "parametro do TA com tipo invalido", c->pc, data[0]);
        if (r.list_end >= 0) holly_intc_raise(Area4Entry::list_end_bit(r.list_end));
        return true;
    }
    case Area4Entry::Path::Texture: {
        uint32_t o[8];
        Area4Entry::texture_offsets(target, o);
        for (int i = 0; i < 8; ++i) std::memcpy(vram_data() + o[i], &data[i], 4);
        vram_touch(o[0], 16);
        vram_touch(o[1], 16);
        return true;
    }
    case Area4Entry::Path::Yuv: break;
    }
    // N8.5 (docs/N8-5-FILMES.md): o conversor YUV so serve o filme legado (Sofdec); no produto os filmes sao nativos e o
    // jogo nunca o usa (sonda e referencias). Um macrobloco aqui e um componente removido: erro explicito.
    ta_yuv_removed(c, "macrobloco no conversor YUV do TA", target & 0x1FFFFFFF);
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

// o conversor YUV so existe no build de validacao (ta_yuv.cpp); no produto, qualquer uso e o erro do N8.5
void     ta_yuv_reset(uint32_t, uint32_t) {}
uint32_t ta_yuv_count() { return 0; }
void     ta_yuv_write(Sh4Context* c, const uint32_t*) { ta_yuv_removed(c, "macrobloco no conversor YUV do TA", 0x10800000); }

} // namespace bs
