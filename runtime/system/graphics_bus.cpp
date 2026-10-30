// N8.7 (docs/N8-7-KAMUI.md secao 12, incremento 3a): no produto, os registradores graficos (005F8000-005F9FFF) sao o
// estado do quadro nativo (native/render/kamui/frame_state): a mesma API de pvr/pvr_regs.h sobre o FrameState, sem o
// banco de hardware do legado (pvr/pvr_regs.cpp, que fica na validacao como referencia). Os efeitos das escritas de
// comando (render, listas, temporizacao do video) rodam na ordem do modelo anterior; os prazos e os relogios nao mudam.
#include "pvr/pvr_regs.h"

#include "cpu/sh4_runtime.h"
#include "cpu/time_hooks.h"
#include "debug/clock_log.h"
#include "debug/gfx_probe.h"
#include "debug/intc_trace.h"
#include "pvr/holly_intc.h"
#include "pvr/pvr_render.h"
#include "pvr/ta.h"
#include "render/kamui/frame_state.h"
#include "system/graphics_ports.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace bs {
namespace {

using native::render::kamui::FrameState;

FrameState                          g_fs(kCompatTicksPerField);
std::array<bool, FrameState::kSize / 4> g_reported{};
PvrStartRenderHook                  g_startHook  = nullptr;
uint64_t                            g_renderDue  = 0;  // instante do fim de render pendente (0: nenhum)
int                                 g_startCount = 0;

uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }

class Effects final : public native::render::kamui::FrameEffects {
public:
    Effects(Sh4Context* c, uint32_t p) : c_(c), p_(p) {}

    void start_render() override {
        ++g_startCount;
        if (g_gfx_probe) gfx_probe_start_render(c_);
        if (g_startHook) g_startHook(c_, g_startCount);
        static const char* stop = std::getenv("BS_STOP_ON_STARTRENDER");
        if (stop && g_startCount == std::atoi(stop)) sh4_fatal(c_, "parada pedida: STARTRENDER", c_->pc, uint32_t(g_startCount));
        // Diagnostico: BS_TA_DUMP=<n> lista os parametros do TA e os registradores de render no n-esimo STARTRENDER
        static const char* dump = std::getenv("BS_TA_DUMP");
        if (dump && g_startCount == std::atoi(dump)) {
            std::fprintf(stderr, "[render] STARTRENDER %d: %zu parametros, %u bytes\n", g_startCount, ta_params().size(),
                         ta_bytes());
            for (uint32_t r : {0x044u, 0x048u, 0x04Cu, 0x050u, 0x054u, 0x05Cu, 0x060u, 0x064u, 0x068u, 0x06Cu, 0x074u,
                               0x078u, 0x07Cu, 0x080u, 0x084u, 0x088u, 0x08Cu, 0x098u, 0x0B0u, 0x0B4u, 0x0B8u, 0x0E4u,
                               0x108u, 0x11Cu, 0x13Cu, 0x140u})
                std::fprintf(stderr, "[render]   reg %08X = %08X\n", FrameState::kBase + r, g_fs.reg(FrameState::kBase + r));
            for (const auto& prm : ta_params()) {
                std::fprintf(stderr, "[render]   L%u", prm.list);
                for (uint32_t i = 0; i < prm.words; ++i) std::fprintf(stderr, " %08X", prm.w[i]);
                std::fprintf(stderr, "\n");
            }
        }
        // o render roda inteiro aqui (a CPU nao observa estados intermediarios); o jogo so ve o resultado depois do fim
        // de render sinalizado
        {
            const auto        t0 = std::chrono::steady_clock::now();
            const RenderStats st = render_pass_run(c_);
            if (std::getenv("BS_PVR_DEBUG"))
                std::fprintf(stderr, "[render] #%d %ux%u tri=%u sprites=%u omitidos=%u (%.2f ms)\n", g_startCount, st.width,
                             st.height, st.triangles, st.sprites, st.skipped,
                             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        // fim de render: a estimativa do Flycast, min(450000 + bytes do TA * 100, 1500000) ciclos; depois, ISTNRM 0-2
        g_renderDue = time_now(c_) + native::render::kamui::render_done_cost(ta_bytes());
        time_provider().render_scheduled(g_renderDue);
    }
    void spg_sync() override { holly_intc_sync(time_now(c_)); }
    void list_init() override {
        if (g_gfx_probe) gfx_probe_list_init();
        ta_list_init();
    }
    void list_continue() override { ta_list_continue(); }
    void spg_changed() override { time_provider().spg_changed(); }
    // N8.5: o conversor YUV so serve o filme legado; no produto, erro explicito
    void yuv(uint32_t, uint32_t) override { ta_yuv_removed(c_, "escrita no conversor YUV do TA", p_); }

private:
    Sh4Context* c_;
    uint32_t    p_;
};

} // namespace

void pvr_regs_reset() {
    g_fs.reset();
    g_renderDue = 0;
    g_reported.fill(false);
    time_provider().render_scheduled(0);  // nenhum fim de render pendente
    time_provider().spg_changed();
}

uint32_t pvr_vblank_in_line() { return g_fs.vblank_in_line(); }
uint32_t pvr_vblank_out_line() { return g_fs.vblank_out_line(); }
bool     pvr_spg_crossed(uint64_t from, uint64_t to, uint32_t line) { return g_fs.spg_crossed(from, to, line); }
uint64_t pvr_spg_next_crossing(uint64_t after, uint32_t line) { return g_fs.spg_next_crossing(after, line); }
uint64_t pvr_spg_next_line_change(uint64_t after) { return g_fs.spg_next_line_change(after); }

SpgPos pvr_spg_position(uint64_t cycles) {
    const auto s = g_fs.spg_position(cycles);
    return {s.line, s.vsync, s.blank};
}

bool pvr_reg_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (!FrameState::contains(p) || size != 4) return false;
    if (p == FrameState::kTaYuvTexCnt) ta_yuv_removed(c, "leitura de TA_YUV_TEX_CNT", p);  // N8.5: so o filme legado le
    // o relogio so e lido para o SPG_STATUS (como no modelo anterior)
    const uint64_t now     = p == FrameState::kSpgStatus ? time_now(c) : 0;
    bool           modeled = true;
    v                      = g_fs.read(p, now, modeled);
    if (p == FrameState::kSpgStatus) {
        if (g_clock_log) clock_log_read(ClockRead::Spg, v);
        if (g_intc_trace) intc_trace_clock(IntcSource::Spg, v);
    }
    if (!modeled && !g_reported[(p - FrameState::kBase) / 4]) {  // latch sem semantica propria: registrar
        g_reported[(p - FrameState::kBase) / 4] = true;
        std::fprintf(stderr, "[pvr] leitura sem modelo: %08X -> %08X (ultimo valor escrito)\n", p, v);
    }
    return true;
}

bool pvr_reg_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (!FrameState::contains(p) || size != 4) return false;
    // Diagnostico: BS_PVR_DEBUG=1 registra a 1a escrita de cada registrador e todo STARTRENDER
    static const bool                             debug = std::getenv("BS_PVR_DEBUG") != nullptr;
    static std::array<bool, FrameState::kSize / 4> seen{};
    if (debug && (!seen[(p - FrameState::kBase) / 4] || p == FrameState::kStartRender)) {
        seen[(p - FrameState::kBase) / 4] = true;
        std::fprintf(stderr, "[pvr] escrita %08X = %08X (ciclo %llu)\n", p, v, (unsigned long long)c->cycles);
    }
    // Diagnostico: BS_PVR_WATCH=<endereco> registra toda escrita nesse registrador
    static const char*    watch = std::getenv("BS_PVR_WATCH");
    static const uint32_t wreg  = watch ? canon(uint32_t(std::strtoul(watch, nullptr, 0))) : 0;
    if (watch && p == wreg)
        std::fprintf(stderr, "[pvr] watch %08X = %08X pr=%08X r15=%08X ciclo %llu\n", p, v, c->pr, c->r[15],
                     (unsigned long long)c->cycles);
    Effects fx(c, p);
    g_fs.write(p, v, fx);
    return true;
}

void pvr_regs_dump(Sh4Context* c, uint32_t* out) {
    for (uint32_t i = 0; i < FrameState::kSize / 4; ++i) {
        const uint32_t p = FrameState::kBase + 4 * i;
        if (p == FrameState::kId || p == FrameState::kRevision || p == FrameState::kSpgStatus) pvr_reg_read(c, p, 4, out[i]);
        else out[i] = g_fs.reg(p);
    }
}

void pvr_regs_load(const uint32_t* words) {
    g_fs.load(words);
    time_provider().spg_changed();
}

void     pvr_set_startrender_hook(PvrStartRenderHook hook) { g_startHook = hook; }
uint32_t pvr_reg(uint32_t a) { return g_fs.reg(a & 0x1FFFFFFF); }
uint32_t pvr_pal_entry(uint32_t index) { return g_fs.palette_argb(index); }
uint64_t pvr_palette_generation() { return g_fs.palette_generation(); }
uint64_t pvr_render_done_due() { return g_renderDue; }
void     pvr_render_done_take() { g_renderDue = 0; }
// a comparacao com o banco do legado so existe na validacao (pvr/pvr_regs.cpp)
std::string pvr_frame_state_summary() { return {}; }

} // namespace bs
