// N8.7 (docs/N8-7-KAMUI.md, incremento 3c): no produto, a saida de video (pvr/pvr_video.h) e o servico nativo de exibicao
// (native/render/kamui/display_service) sobre o estado do quadro (system/graphics_bus) e a memoria das texturas
// (system/texture_heap_bus). A saida de video do legado (pvr/pvr_video.cpp) fica na validacao como referencia.
#include "pvr/pvr_video.h"

#include "pvr/pvr_regs.h"
#include "pvr/vram.h"
#include "render/kamui/display_service.h"

namespace bs {
namespace {

PvrVblankHook g_hook    = nullptr;
void*         g_hookArg = nullptr;

} // namespace

void pvr_video_frame(std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height) {
    namespace k = native::render::kamui;
    k::scanout(k::DisplayRegs::from([](uint32_t a) { return pvr_reg(a); }), vram_data(), argb, width, height);
}

std::string pvr_display_summary() { return {}; }  // a comparacao com a saida do legado so existe na validacao

void pvr_set_vblank_hook(PvrVblankHook fn, void* arg) {
    g_hook    = fn;
    g_hookArg = arg;
}

void pvr_video_vblank() {
    if (g_hook) g_hook(g_hookArg);
}

} // namespace bs
