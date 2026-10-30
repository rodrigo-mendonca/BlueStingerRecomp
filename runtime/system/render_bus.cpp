// N8.7 (docs/N8-7-KAMUI.md, incremento 4d): no produto, o render pedido por STARTRENDER (pvr/pvr_render.h) e a passagem
// nativa (native/render/kamui/render_pass) sobre o estado do quadro (pvr_reg, o FrameState do 3a) e a memoria das
// texturas (vram_data/vram_touch, a TextureHeap do 3b): tamanho, fundo, parametros do quadro, Region Array, as listas do
// Kamui pelo ListSink para a IR, o desenho pela IR (o PvrListRenderer, transitional/render_core) e a gravacao no
// framebuffer. O rasterizador de
// referencia (pvr/pvr_render.cpp, com o caminho do TA) fica na validacao.
#include "pvr/pvr_render.h"

#include "cpu/sh4_runtime.h"
#include "pvr/pvr_regs.h"
#include "pvr/ta.h"
#include "pvr/vram.h"
#include "render/kamui/render_pass.h"
#include "render/soft/soft_raster.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace bs {
namespace {

using native::render::kamui::RenderPass;

PvrListRenderer* g_renderer = nullptr;

uint32_t reg(void*, uint32_t p) { return pvr_reg(p); }
void     touch(void*, uint32_t off, uint32_t n) { vram_touch(off, n); }

void note(const char* what) {
    static std::vector<const char*> seen;
    for (const char* s : seen)
        if (s == what) return;
    seen.push_back(what);
    std::fprintf(stderr, "[render] ainda nao suportado: %s (omitido)\n", what);
}

uint32_t background() {
    const RenderPass::Background b = RenderPass::background(reg, nullptr, vram_data());
    if (b.textured) note("fundo com textura");
    return b.color;
}

} // namespace

void     pvr_render_set_renderer(PvrListRenderer* renderer) { g_renderer = renderer; }
uint32_t pvr_background_color() { return background(); }

native::render::FrameParams pvr_frame_params(uint32_t width, uint32_t height) {
    return RenderPass::frame_params(reg, nullptr, width, height, background());
}

std::string pvr_render_pass_summary() { return {}; }  // a comparacao com o rasterizador de referencia so na validacao

RenderStats pvr_render(Sh4Context* c) {
    const native::render::soft::FpEnv fpEnv;  // ambiente fixo nesta thread (os workers instalam o seu)
    RenderStats                       st;
    const RenderPass::Size            sz = RenderPass::size(reg, nullptr);
    st.width = sz.width, st.height = sz.height;
    // o fundo pelo plano de ISP_BACKGND_T (o alfa e sempre 0xFF), antes do Region Array, como antes
    const uint32_t bg = background() & 0xFFFFFF;
    // ISP_FEED_CFG (0x098) bit 0: 1 = pre-sort (translucidos na ordem de submissao), 0 = autosort por pixel
    const bool autosort = !(pvr_reg(0x005F8098) & 1);
    if (RenderPass::region_sort_mismatch(reg, nullptr, vram_data(), autosort))
        note("Region Array com modo de ordenacao por tile diferente do global");
    std::vector<uint32_t> color(size_t(st.width) * st.height, 0xFF000000 | bg);
    // as listas do Kamui direto para a IR (ListSink); os parametros do quadro vem dos registradores
    native::render::kamui::ListFrame lf;
    ta_list_sink().build(pvr_frame_params(st.width, st.height), (pvr_reg(0x005F80E4) & 0x1F) * 32, lf);
    st.triangles = lf.triangles, st.sprites = lf.sprites, st.skipped = lf.skipped, st.unsupported = lf.unsupported;
    st.two_volume = lf.two_volume;
    // Diagnostico: BS_RENDER_PROFILE=1 mede as fases (ms); no produto so a decodificacao (o desenho e da IR)
    static const bool prof = std::getenv("BS_RENDER_PROFILE") != nullptr;
    using Clock            = std::chrono::steady_clock;
    auto   tp              = Clock::now();
    double ms[5]           = {};
    ms[0] += std::chrono::duration<double, std::milli>(Clock::now() - tp).count();
    // o produto desenha so pela IR (render_core, Software ou Native). Um render que a IR nao desenhou e erro
    // explicito (nunca um recuo silencioso)
    const bool replaced = g_renderer && g_renderer->render_list(lf, st.width, st.height, color);
    if (!replaced) sh4_fatal(c, "render sem o renderizador sobre a IR (o caminho do TA saiu do produto no N8.7)", c ? c->pc : 0, 0);
    // revisao Codex 20261028-115305: um poligono que o PVR desenharia mas a IR nao representa (bump, YUV twiddled,
    // paleta de 4 bits com mipmap, dois volumes) e erro explicito no produto, nunca um quadro publicado sem ele
    if (st.unsupported) {
        static char msg[160];
        std::snprintf(msg, sizeof msg, "render com %u vertices de estados que a IR nao representa (textura bump, YUV twiddled, "
                                       "paleta de 4 bits com mipmap ou dois volumes)", st.unsupported);
        sh4_fatal(c, msg, c ? c->pc : 0, st.unsupported);
    }
    RenderPass::write_back(reg, nullptr, color, st.width, st.height, vram_data(), touch);
    if (prof)
        std::fprintf(stderr, "[perfil] parse %.2f opaca %.2f pt %.2f transl %.2f resolve %.2f ms; fragmentos %zu\n",
                     ms[0], ms[1], ms[2], ms[3], ms[4], size_t(0));
    return st;
}

bool pvr_render_take_fp_violation() { return native::render::soft::take_fp_violation(); }

} // namespace bs
