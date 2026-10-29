// Registradores do PowerVR (Holly). Layout segundo a documentacao de registradores
// do KallistiOS (referencia de comportamento; nenhum codigo copiado):
//   ID         0x005F8000: 0x17FD11DB (so leitura)
//   REVISION   0x005F8004: 0x00000011 (so leitura); valores lidos pelo boot do jogo
//                          (8C1C59D6) no Flycast, que decide o caminho pela revisao
//   SPG_VBLANK_INT 0x005F80CC: bits 25-16 linha do VBlank-Out, 9-0 do VBlank-In
//   SPG_LOAD   0x005F80D8: bits 25-16 linhas por campo - 1
//   SPG_VBLANK 0x005F80DC: bits 25-16 fim (vbend), bits 9-0 inicio (vstart) do vblank
//   SPG_CONTROL 0x005F80D0: bit 4 entrelacado
//   SPG_STATUS 0x005F810C: bits 9-0 linha atual, 10 campo, 13 vsync (so leitura)
// (Ate 2026-10-14 os tres primeiros estavam em 0x0C4/0x0CC/0x0C8, enderecos errados;
// achado comparando o banco de registradores com o Flycast.)
//
// Modelo de tempo (aproximado, R1 do ARCHITECTURE.md): o campo dura 1/59.94 s de
// CPU a 200 MHz, dividido igualmente entre as linhas. O bit 13 de SPG_STATUS fica
// ligado de vstart ate vbend (o mesmo que o Flycast faz). Os ciclos do codigo gerado
// sao estimados, entao a fase exata nunca coincide com o Flycast.
#include "pvr/pvr_regs.h"
#include "cpu/time_hooks.h"
#include "debug/clock_log.h"
#include "debug/gfx_probe.h"
#include "debug/intc_trace.h"

#include "pvr/holly_intc.h"
#include "pvr/pvr_render.h"
#include "pvr/ta.h"
#include "cpu/sh4_runtime.h"
#include "render/kamui/frame_state.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace bs {
namespace {

constexpr uint32_t kBase      = 0x005F8000;
constexpr uint32_t kSize      = 0x2000;
constexpr uint32_t kId          = 0x005F8000;
constexpr uint32_t kRevision    = 0x005F8004;
constexpr uint32_t kSpgVblankInt = 0x005F80CC;
constexpr uint32_t kSpgVblank = 0x005F80DC;
constexpr uint32_t kSpgControl = 0x005F80D0;
constexpr uint32_t kSpgLoad   = 0x005F80D8;
constexpr uint32_t kSpgStatus = 0x005F810C;
constexpr uint32_t kSoftReset  = 0x005F8008;  // comando: nao guarda o valor escrito
constexpr uint32_t kStartRender = 0x005F8014; // comando: nao guarda o valor escrito
constexpr uint32_t kTaIspBase  = 0x005F8128;
constexpr uint32_t kTaNextOpb  = 0x005F8134;  // ponteiro mantido pelo TA
constexpr uint32_t kTaItpCurrent = 0x005F8138; // ponteiro mantido pelo TA
constexpr uint32_t kTaYuvTexBase = 0x005F8148;
constexpr uint32_t kTaYuvTexCtrl = 0x005F814C;
constexpr uint32_t kTaYuvTexCnt = 0x005F8150;  // so leitura
constexpr uint32_t kTaNextOpbInit = 0x005F8164;
constexpr uint32_t kTaListInit = 0x005F8144;  // bit 31: inicia listas novas (comando)
constexpr uint32_t kTaListCont = 0x005F8160;  // bit 31: continua as listas

// campo NTSC 59.94 Hz na base de tempo de compatibilidade (cpu/time_hooks.h)
constexpr uint64_t kFieldCycles    = kCompatTicksPerField;
constexpr uint32_t kDefaultLines   = 263;                    // NTSC nao entrelacado

std::array<uint32_t, kSize / 4> g_regs{};
std::array<bool, kSize / 4>     g_reported{};
// Fase do feixe no ciclo 0 (em ciclos de CPU, dentro de dois campos): vem do SPG_STATUS
// deixado pelo BIOS (linha + campo), carregado com o banco de registradores.
uint64_t                        g_phase = 0;
PvrStartRenderHook              g_startHook = nullptr;
uint64_t                        g_renderDue = 0;  // ciclo do fim de render pendente (0: nenhum)
int                             g_startCount = 0;

uint32_t reg(uint32_t a) { return g_regs[(a - kBase) / 4]; }

// N8.7 (docs/N8-7-KAMUI.md, incremento 3a): este banco e a referencia; o FrameState nativo (o do produto) roda em sombra,
// recebe cada acesso e e comparado: valores lidos (SPG_STATUS no mesmo instante), o registrador gravado e os ponteiros do
// TA, o banco inteiro a cada STARTRENDER e as contas da temporizacao do video. Negativos (BS_FRAME_STATE_MUTACAO):
// leitura (o valor lido da sombra + 1), escrita (a sombra perde uma escrita de dado simples), fase (a fase do feixe
// carregada errada), efeitos (a sombra nao pede a sincronizacao do INTC), prazo (a formula do produto + 1 ciclo)
native::render::kamui::FrameState g_shadow(kFieldCycles);
uint64_t                          g_shadowChecks = 0, g_shadowMismatch = 0;
// revisao Codex 20261028-212527: os efeitos de cada escrita, na ordem (R render, S sincronizacao do INTC, I listas novas,
// C listas continuadas, G temporizacao nova, Y YUV): o legado registra os que executa, a sombra os que pede
std::string g_fxLegacy, g_fxShadow;
uint64_t    g_renderNow = 0;  // o instante em que o legado calculou o prazo do fim de render
std::string                       g_shadowFirst;

const std::string& shadow_mutation() {
    static const std::string m = [] {
        const char* v = std::getenv("BS_FRAME_STATE_MUTACAO");
        return std::string(v ? v : "");
    }();
    return m;
}

void shadow_check(bool same, const char* what, uint32_t p, uint64_t a, uint64_t b) {
    ++g_shadowChecks;
    if (same) return;
    ++g_shadowMismatch;
    if (g_shadowFirst.empty()) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s %08X: legado %llX, FrameState %llX", what, p, (unsigned long long)a,
                      (unsigned long long)b);
        g_shadowFirst = buf;
    }
}

class NoEffects final : public native::render::kamui::FrameEffects {
public:
    void start_render() override { g_fxShadow += 'R'; }
    void spg_sync() override { g_fxShadow += shadow_mutation() == "efeitos" ? "" : "S"; }
    void list_init() override { g_fxShadow += 'I'; }
    void list_continue() override { g_fxShadow += 'C'; }
    void spg_changed() override { g_fxShadow += 'G'; }
    void yuv(uint32_t, uint32_t) override { g_fxShadow += 'Y'; }
};

// Area 0 vista por P0-P3 (o jogo usa P2, 0xA05F....).
uint32_t canon(uint32_t a) { return a < 0xE0000000 ? (a & 0x1FFFFFFF) : 0; }

uint32_t fieldLines() {
    const uint32_t lines = ((reg(kSpgLoad) >> 16) & 0x3FF) + 1;
    return lines < 2 ? kDefaultLines : lines;
}

} // namespace

void pvr_regs_reset() {
    g_shadow.reset();
    g_regs.fill(0);
    g_phase = 0;
    g_renderDue = 0;
    g_reported.fill(false);
    time_provider().render_scheduled(0);  // nenhum fim de render pendente
    time_provider().spg_changed();
}

uint32_t pvr_vblank_in_line() { return reg(kSpgVblankInt) & 0x3FF; }
uint32_t pvr_vblank_out_line() { return (reg(kSpgVblankInt) >> 16) & 0x3FF; }

static bool pvr_spg_crossed_legado(uint64_t from, uint64_t to, uint32_t line) {
    if (to <= from) return false;
    from += g_phase;
    to += g_phase;
    const uint32_t lines = fieldLines();
    if (line >= lines) return false;  // linha que o campo nunca alcanca
    const uint64_t off = uint64_t(line) * kFieldCycles / lines;
    // eventos em off + k*campo; existe algum em (from, to]?
    auto count = [&](uint64_t t) { return t < off ? 0ull : (t - off) / kFieldCycles + 1; };
    return count(to) > count(from);
}

static uint64_t pvr_spg_next_crossing_legado(uint64_t after, uint32_t line) {
    const uint32_t lines = fieldLines();
    if (line >= lines) return kSpgNever;
    const uint64_t off = uint64_t(line) * kFieldCycles / lines;
    // mesma conta de pvr_spg_crossed: eventos em t com t + fase = off + k*campo, k >= 0
    const uint64_t x = after + g_phase;
    if (x < off) return off - g_phase;
    const uint64_t k = (x - off) / kFieldCycles + 1;
    return off + k * kFieldCycles - g_phase;
}

static uint64_t pvr_spg_next_line_change_legado(uint64_t after) {
    // a mesma conta de pvr_spg_position: a linha muda na primeira posicao do campo com
    // (posicao * linhas / campo) >= linha + 1, ou seja, em teto((linha + 1) * campo / linhas). (O
    // inicio de linha de pvr_spg_next_crossing e truncado: no limite, a linha ainda nao mudou.)
    const uint32_t lines = fieldLines();
    const uint64_t x     = after + g_phase;
    const uint64_t base  = x - x % kFieldCycles;
    const uint32_t line  = uint32_t((x % kFieldCycles) * lines / kFieldCycles);
    const uint64_t next  = line + 1 >= lines ? kFieldCycles : (uint64_t(line + 1) * kFieldCycles + lines - 1) / lines;
    return base + next - g_phase;
}

static SpgPos pvr_spg_position_legado(uint64_t cycles) {
    cycles += g_phase;
    const uint32_t lines   = fieldLines();
    const uint32_t vbstart = reg(kSpgVblank) & 0x3FF;
    const uint32_t vbend   = (reg(kSpgVblank) >> 16) & 0x3FF;

    SpgPos p;
    p.line  = uint32_t((cycles % kFieldCycles) * lines / kFieldCycles);
    p.vsync = vbstart > vbend ? (p.line >= vbstart || p.line < vbend) : (p.line >= vbstart && p.line < vbend);
    p.blank = p.vsync;
    return p;
}

bool pvr_spg_crossed(uint64_t from, uint64_t to, uint32_t line) {
    const bool r = pvr_spg_crossed_legado(from, to, line);
    shadow_check(r == g_shadow.spg_crossed(from, to, line), "spg_crossed", line, r, g_shadow.spg_crossed(from, to, line));
    return r;
}
uint64_t pvr_spg_next_crossing(uint64_t after, uint32_t line) {
    const uint64_t r = pvr_spg_next_crossing_legado(after, line), n = g_shadow.spg_next_crossing(after, line);
    shadow_check(r == n, "spg_next_crossing", line, r, n);
    return r;
}
uint64_t pvr_spg_next_line_change(uint64_t after) {
    const uint64_t r = pvr_spg_next_line_change_legado(after), n = g_shadow.spg_next_line_change(after);
    shadow_check(r == n, "spg_next_line_change", 0, r, n);
    return r;
}
SpgPos pvr_spg_position(uint64_t cycles) {
    const SpgPos r = pvr_spg_position_legado(cycles);
    const auto   n = g_shadow.spg_position(cycles);
    shadow_check(r.line == n.line && r.vsync == n.vsync && r.blank == n.blank, "spg_position", r.line, r.line, n.line);
    return r;
}

static uint64_t g_readNow = 0;  // o instante usado pela ultima leitura do SPG_STATUS (para a sombra)

static bool reg_read_legado(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v);

bool pvr_reg_read(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    if (!reg_read_legado(c, a, size, v)) return false;
    const uint32_t p = canon(a);
    if (p != kTaYuvTexCnt) {  // o contador do YUV e do conversor (so o filme legado o le)
        bool     modeled = true;
        uint32_t n       = g_shadow.read(p, p == kSpgStatus ? g_readNow : 0, modeled);
        if (shadow_mutation() == "leitura") ++n;
        shadow_check(n == v, "leitura", p, v, n);
    }
    return true;
}

static bool reg_read_legado(Sh4Context* c, uint32_t a, unsigned size, uint32_t& v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    if (p == kId) {
        v = 0x17FD11DB;
        return true;
    }
    if (p == kRevision) {
        v = 0x00000011;
        return true;
    }
    if (p == kTaYuvTexCnt) {
#if !BS_VALIDACAO
        ta_yuv_removed(c, "leitura de TA_YUV_TEX_CNT", p);  // N8.5: so o filme legado le
#endif
        v = ta_yuv_count();
        return true;
    }
    if (p == kSpgStatus) {
        const uint64_t now = time_now(c);
        g_readNow          = now;
        const SpgPos   s   = pvr_spg_position(now);
        // bit 10: campo (alterna a cada campo com SPG_CONTROL.interlace, bit 4); bit 11
        // nunca aceso no Flycast
        const bool interlace = reg(kSpgControl) & 0x10;
        const bool field     = interlace && (((now + g_phase) / kFieldCycles) & 1);
        v = (s.line & 0x3FF) | (field ? 1u << 10 : 0) | (s.vsync ? 1u << 13 : 0);
        if (g_clock_log) clock_log_read(ClockRead::Spg, v);
        if (g_intc_trace) intc_trace_clock(IntcSource::Spg, v);
        return true;
    }
    v = reg(p);
    if (!g_reported[(p - kBase) / 4]) {  // latch sem modelo proprio: registrar (ver holly_sb.cpp)
        g_reported[(p - kBase) / 4] = true;
        std::fprintf(stderr, "[pvr] leitura sem modelo: %08X -> %08X (ultimo valor escrito)\n", p, v);
    }
    return true;
}

void pvr_set_startrender_hook(PvrStartRenderHook hook) { g_startHook = hook; }

uint32_t pvr_reg(uint32_t a) { return reg(a & 0x1FFFFFFF); }

// PAL_RAM_CTRL (0x005F8108) bits 1-0: 0 ARGB1555, 1 RGB565, 2 ARGB4444, 3 ARGB8888.
uint32_t pvr_pal_entry(uint32_t index) {
    const uint32_t e   = reg(0x005F9000 + 4 * (index & 1023));
    const uint32_t fmt = reg(0x005F8108) & 3;
    if (fmt == 3) return e;
    auto x = [](uint32_t v, int bits) {
        v <<= 8 - bits;
        return v | (v >> bits);
    };
    uint32_t a, r, g, b;
    if (fmt == 0) a = (e & 0x8000) ? 0xFF : 0, r = x((e >> 10) & 31, 5), g = x((e >> 5) & 31, 5), b = x(e & 31, 5);
    else if (fmt == 1) a = 0xFF, r = x((e >> 11) & 31, 5), g = x((e >> 5) & 63, 6), b = x(e & 31, 5);
    else a = x((e >> 12) & 15, 4), r = x((e >> 8) & 15, 4), g = x((e >> 4) & 15, 4), b = x(e & 15, 4);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint64_t pvr_render_done_due() { return g_renderDue; }
void     pvr_render_done_take() { g_renderDue = 0; }

void pvr_regs_load(const uint32_t* words) {
    if (shadow_mutation() == "fase") {
        std::array<uint32_t, kSize / 4> w;
        std::copy(words, words + kSize / 4, w.begin());
        w[(kSpgStatus - kBase) / 4] += 7;  // a sombra comeca sete linhas adiante
        g_shadow.load(w.data());
    } else {
        g_shadow.load(words);
    }
    for (uint32_t i = 0; i < kSize / 4; ++i) g_regs[i] = words[i];
    const uint32_t status = words[(kSpgStatus - kBase) / 4];
    const uint32_t lines  = fieldLines();
    const uint32_t line   = std::min<uint32_t>(status & 0x3FF, lines - 1);
    g_phase = ((status >> 10) & 1) * kFieldCycles + (uint64_t(line) * kFieldCycles + lines - 1) / lines;
    time_provider().spg_changed();
}

void pvr_regs_dump(Sh4Context* c, uint32_t* out) {
    for (uint32_t i = 0; i < kSize / 4; ++i) {
        const uint32_t p = kBase + 4 * i;
        if (p == kId || p == kRevision || p == kSpgStatus) pvr_reg_read(c, p, 4, out[i]);
        else out[i] = g_regs[i];  // sem o registro de "leitura sem modelo"
    }
}

static bool reg_write_legado(Sh4Context* c, uint32_t a, unsigned size, uint32_t v);

bool pvr_reg_write(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    g_fxLegacy.clear(), g_fxShadow.clear();
    if (!reg_write_legado(c, a, size, v)) return false;
    const uint32_t p = canon(a);
    NoEffects      fx;
    static uint64_t plain = 0;
    const bool      skip  = shadow_mutation() == "escrita" && p == 0x005F8060 && ++plain == 3;  // FB_W_SOF1, gravado a cada render
    if (!skip) g_shadow.write(p, v, fx);
    shadow_check(g_shadow.reg(p) == reg(p), "escrita", p, reg(p), g_shadow.reg(p));
    shadow_check(g_fxLegacy == g_fxShadow, ("efeitos " + g_fxLegacy + " x " + g_fxShadow).c_str(), p, g_fxLegacy.size(),
                 g_fxShadow.size());
    if (p == kStartRender) {  // o prazo do fim de render: o instante do legado + a formula compartilhada do produto
        const uint64_t cost = native::render::kamui::render_done_cost(ta_bytes()) + (shadow_mutation() == "prazo" ? 1 : 0);
        shadow_check(g_renderDue == g_renderNow + cost, "prazo do fim de render", p, g_renderDue, g_renderNow + cost);
    }
    if (p == kTaListInit) {
        shadow_check(g_shadow.reg(kTaItpCurrent) == reg(kTaItpCurrent), "TA_ITP_CURRENT", kTaItpCurrent, reg(kTaItpCurrent),
                     g_shadow.reg(kTaItpCurrent));
        shadow_check(g_shadow.reg(kTaNextOpb) == reg(kTaNextOpb), "TA_NEXT_OPB", kTaNextOpb, reg(kTaNextOpb),
                     g_shadow.reg(kTaNextOpb));
    }
    if (p == kStartRender)  // o banco inteiro a cada render
        for (uint32_t i = 0; i < kSize / 4; ++i) {
            const uint32_t q = kBase + 4 * i;
            if (g_regs[i] != g_shadow.reg(q)) {
                shadow_check(false, "banco", q, g_regs[i], g_shadow.reg(q));
                break;
            }
        }
    for (uint32_t i = 0; i < 1024; i += 97)  // amostra da paleta convertida
        if (p == kStartRender) shadow_check(pvr_pal_entry(i) == g_shadow.palette_argb(i), "paleta", i, pvr_pal_entry(i),
                                            g_shadow.palette_argb(i));
    return true;
}

std::string pvr_frame_state_summary() {
    char b[400];
    std::snprintf(b, sizeof b, "[frame-state] comparacoes %llu, diferentes %llu%s%s\n", (unsigned long long)g_shadowChecks,
                  (unsigned long long)g_shadowMismatch, g_shadowFirst.empty() ? "" : "; primeira: ", g_shadowFirst.c_str());
    return b;
}

static bool reg_write_legado(Sh4Context* c, uint32_t a, unsigned size, uint32_t v) {
    const uint32_t p = canon(a);
    if (p < kBase || p >= kBase + kSize || size != 4) return false;
    // Diagnostico: BS_PVR_DEBUG=1 registra a 1a escrita de cada registrador e todo STARTRENDER
    static const bool debug = std::getenv("BS_PVR_DEBUG") != nullptr;
    static std::array<bool, kSize / 4> seen{};
    if (debug && (!seen[(p - kBase) / 4] || p == 0x005F8014)) {
        seen[(p - kBase) / 4] = true;
        std::fprintf(stderr, "[pvr] escrita %08X = %08X (ciclo %llu)\n", p, v, (unsigned long long)c->cycles);
    }
    // Diagnostico: BS_PVR_WATCH=<endereco> registra toda escrita nesse registrador
    static const char*    watch = std::getenv("BS_PVR_WATCH");
    static const uint32_t wreg  = watch ? canon(uint32_t(std::strtoul(watch, nullptr, 0))) : 0;
    if (watch && p == wreg)
        std::fprintf(stderr, "[pvr] watch %08X = %08X pr=%08X r15=%08X ciclo %llu\n", p, v, c->pr, c->r[15],
                     (unsigned long long)c->cycles);
    // Testes de integracao: BS_STOP_ON_STARTRENDER=<n> para na n-esima escrita em STARTRENDER
    if (p == 0x005F8014) {
        g_fxLegacy += 'R';
        ++g_startCount;
        if (g_gfx_probe) gfx_probe_start_render(c);
        if (g_startHook) g_startHook(c, g_startCount);
        static const char* stop = std::getenv("BS_STOP_ON_STARTRENDER");
        if (stop && g_startCount == std::atoi(stop))
            sh4_fatal(c, "parada pedida: STARTRENDER", c->pc, uint32_t(g_startCount));
        // Diagnostico: BS_TA_DUMP=<n> lista os parametros do TA e os registradores de render
        // no n-esimo STARTRENDER
        static const char* dump = std::getenv("BS_TA_DUMP");
        if (dump && g_startCount == std::atoi(dump)) {
            std::fprintf(stderr, "[render] STARTRENDER %d: %zu parametros, %u bytes\n", g_startCount, ta_params().size(),
                         ta_bytes());
            for (uint32_t r : {0x044u, 0x048u, 0x04Cu, 0x050u, 0x054u, 0x05Cu, 0x060u, 0x064u, 0x068u, 0x06Cu, 0x074u,
                               0x078u, 0x07Cu, 0x080u, 0x084u, 0x088u, 0x08Cu, 0x098u, 0x0B0u, 0x0B4u, 0x0B8u, 0x0E4u,
                               0x108u, 0x11Cu, 0x13Cu, 0x140u})
                std::fprintf(stderr, "[render]   reg %08X = %08X\n", kBase + r, reg(kBase + r));
            for (const auto& prm : ta_params()) {
                std::fprintf(stderr, "[render]   L%u", prm.list);
                for (uint32_t i = 0; i < prm.words; ++i) std::fprintf(stderr, " %08X", prm.w[i]);
                std::fprintf(stderr, "\n");
            }
        }
        // O render roda inteiro aqui (a CPU nao observa estados intermediarios); o
        // jogo so ve o resultado depois do fim de render sinalizado abaixo.
        {
            const auto        t0 = std::chrono::steady_clock::now();
            const RenderStats st = pvr_render(c);
            if (std::getenv("BS_PVR_DEBUG"))
                std::fprintf(stderr, "[render] #%d %ux%u tri=%u sprites=%u omitidos=%u (%.2f ms)\n", g_startCount,
                             st.width, st.height, st.triangles, st.sprites, st.skipped,
                             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        // Fim de render: o hardware nao documenta a duracao; mesma estimativa do Flycast
        // (spg.cpp, scheduleRenderDone): min(450000 + bytes do TA * 100, 1500000) ciclos.
        // Aproximacao. Depois disso, ISTNRM bits 0 (video), 1 (ISP) e 2 (TSP).
        const uint64_t cost = std::min<uint64_t>(450000 + uint64_t(ta_bytes()) * 100, 1500000);
        g_renderNow         = time_now(c);
        g_renderDue         = g_renderNow + cost;
        time_provider().render_scheduled(g_renderDue);
    }
    if (p == kSpgLoad || p == kSpgVblankInt) holly_intc_sync(time_now(c)), g_fxLegacy += 'S';
    if (p == kTaListInit && (v & 0x80000000)) {
        if (g_gfx_probe) gfx_probe_list_init();
        ta_list_init();
        g_fxLegacy += 'I';
        // o TA recomeca nos ponteiros iniciais; o jogo le TA_ITP_CURRENT para saber onde
        // gravar o poligono de fundo (aproximacao: no hardware ele avanca com os
        // parametros gravados; o Flycast, o oraculo, nao o avanca)
        g_regs[(kTaNextOpb - kBase) / 4]    = reg(kTaNextOpbInit);
        g_regs[(kTaItpCurrent - kBase) / 4] = reg(kTaIspBase);
    }
    if (p == kTaListCont) ta_list_continue(), g_fxLegacy += 'C';  // qualquer escrita (como no Flycast)
    // registradores de comando e so de leitura nao guardam o valor escrito
    const bool command = p == kTaListInit || p == kStartRender || p == kSoftReset;
    const bool readOnly = p == kSpgStatus || p == kId || p == kRevision || p == kTaYuvTexCnt;
    if (!command && !readOnly) g_regs[(p - kBase) / 4] = v;
    if (p == kSpgLoad || p == kSpgVblankInt) time_provider().spg_changed(), g_fxLegacy += 'G';  // nova temporizacao do SPG
#if !BS_VALIDACAO
    if (p == kTaYuvTexBase || p == kTaYuvTexCtrl) ta_yuv_removed(c, "escrita no conversor YUV do TA", p);  // N8.5
#endif
    if (p == kTaYuvTexBase || p == kTaYuvTexCtrl) ta_yuv_reset(reg(kTaYuvTexBase), reg(kTaYuvTexCtrl)), g_fxLegacy += 'Y';
    return true;
}

} // namespace bs
