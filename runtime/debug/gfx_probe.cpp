#include "debug/gfx_probe.h"

#include "cpu/sh4_runtime.h"
#include "pvr/pvr_regs.h"
#include "pvr/vram.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>

namespace bs {

bool g_gfx_probe = false;

namespace {

constexpr uint32_t kIsr = 0x8C1C53E2;  // tratador do Holly do Kamui (docs/N8-7-KAMUI.md secao 1.2)

const char* region_of(uint32_t p) {
    const uint32_t r = p & 0x01800000;
    return r == 0 ? "poligonos" : (p & 0x01000000) ? "textura" : "yuv";
}

struct Agg {
    uint64_t n = 0, bytes = 0;
    uint32_t min = UINT32_MAX, max = 0;
    void add(uint32_t b) { ++n, bytes += b, min = std::min(min, b), max = std::max(max, b); }
};

struct RegUse {
    uint64_t           n = 0;
    std::set<uint32_t> values;  // amostra (ate 8)
};

struct State {
    std::mutex mu;
    std::string path;
    bool        atexitDone = false;
    // area 4 por (caminho, funcao, regiao)
    std::map<std::tuple<char, uint32_t, std::string>, uint64_t> ta;
    // canal 2 por (funcao, regiao destino)
    std::map<std::pair<uint32_t, std::string>, Agg> dma;
    // registradores por (funcao, registrador, escrita)
    std::map<std::tuple<uint32_t, uint32_t, bool>, RegUse> regs;
    // VRAM pelo caminho de memoria por (funcao, caminho 32/64)
    std::map<std::pair<uint32_t, int>, uint64_t> vram;
    // bytes de poligono por render (desde o TA_LIST_INIT), por caminho
    uint64_t poly_sq = 0, poly_dma = 0;
    Agg      render_sq, render_dma, render_total;
    uint64_t renders = 0, list_inits = 0;
    // alvos de render (FB_W_SOF1) e quadros exibidos (FB_R_SOF1/2)
    std::map<uint32_t, uint64_t> targets, displayed;
    std::map<uint32_t, uint64_t> fb_w_ctrl, fb_w_stride;
    uint64_t                     sof1_bit24 = 0;
    // texturas amostradas dentro de um alvo de render
    std::map<uint32_t, uint64_t> rtt_hits;  // TCW -> vezes
    uint64_t                     textures = 0;
    // tratador do Kamui: episodios
    bool                                 in_isr = false;
    std::string                          seq;
    std::map<std::string, uint64_t>      isr_seqs;
    uint64_t                             isr_calls = 0;
};

State& state() {
    static State s;
    return s;
}

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

// revisao Codex 20261028-173027: a sonda encadeia a substituicao que ja existia no endereco (mod ou provedor) e a
// devolve ao desligar; se outra substituicao tomar o lugar depois, o resumo diz que as sequencias ficaram incompletas
Sh4Func g_prev = nullptr;

void isr_wrapper(Sh4Context* c) {
    State& s = state();
    {
        std::lock_guard lk(s.mu);
        s.in_isr = true;
        s.seq.clear();
        ++s.isr_calls;
    }
    const Sh4Func next = g_prev ? g_prev : sh4_lookup_generated(kIsr);
    if (!next) sh4_fatal(c, "fase 0: tratador do Kamui sem codigo gerado", c->pc, kIsr);
    next(c);
    std::lock_guard lk(s.mu);
    s.in_isr = false;
    ++s.isr_seqs[s.seq.empty() ? std::string("(sem acesso)") : s.seq];
}

void atExit() { gfx_probe_flush(); }

const bool kInit = [] {
    if (const char* p = std::getenv("BS_HW_PROBE_GRAFICOS"); p && *p) gfx_probe_init(p);
    return true;
}();

// a origem da escrita no TA: a funcao do guest em execucao (sonda N0: cur_fn)
uint32_t fn_of(const Sh4Context* c) { return c ? c->cur_fn : 0u; }

} // namespace

void gfx_probe_init(const std::string& path) {
    State& s = state();
    {
        std::lock_guard lk(s.mu);
        s.path = path;
        if (!path.empty() && !s.atexitDone) {
            std::atexit(atExit);
            s.atexitDone = true;
        }
    }
    g_gfx_probe = !path.empty();
    // o despachante 8C1982F4 chama o tratador por ponteiro: a substituicao (desvio indireto) envolve o gerado
    static const bool semTratador = std::getenv("BS_HW_PROBE_GRAFICOS_SEM_TRATADOR") != nullptr;  // bisseccao
    const Sh4Func     cur         = sh4_override_get(kIsr);
    if (g_gfx_probe && !semTratador) {
        if (cur != isr_wrapper) g_prev = cur, sh4_override(kIsr, isr_wrapper);
    } else if (cur == isr_wrapper) {
        sh4_override(kIsr, g_prev);  // devolve o que havia antes (ou nada)
        g_prev = nullptr;
    }
}

void gfx_probe_ta(const Sh4Context* c, uint32_t target, GfxPath path) {
    const uint32_t p = target & 0x1FFFFFFF;
    State&         s = state();
    std::lock_guard lk(s.mu);
    ++s.ta[{char(path), fn_of(c), region_of(p)}];
    if ((p & 0x01800000) == 0) (path == GfxPath::Dma ? s.poly_dma : s.poly_sq) += 32;
}

void gfx_probe_dma(const Sh4Context* c, uint32_t src, uint32_t dest, uint32_t len) {
    (void)src;
    State&          s = state();
    std::lock_guard lk(s.mu);
    s.dma[{fn_of(c), region_of(dest & 0x1FFFFFFF)}].add(len);
    if (s.in_isr) s.seq += " DMA:" + std::string(region_of(dest & 0x1FFFFFFF)) + "/" + std::to_string(len);
}

void gfx_probe_reg(const Sh4Context* c, uint32_t reg, bool write, uint32_t v) {
    State&          s = state();
    std::lock_guard lk(s.mu);
    RegUse&         u = s.regs[{fn_of(c), reg, write}];
    ++u.n;
    if (u.values.size() < 8) u.values.insert(v);
    if (s.in_isr && s.seq.size() < 600) s.seq += std::string(write ? " W" : " R") + hex(reg) + "=" + hex(v);
}

void gfx_probe_vram_write(const Sh4Context* c, uint32_t addr, unsigned size) {
    (void)size;
    State&          s = state();
    std::lock_guard lk(s.mu);
    ++s.vram[{fn_of(c), (addr & 0x01000000) ? 32 : 64}];
}

void gfx_probe_list_init() {
    State&          s = state();
    std::lock_guard lk(s.mu);
    ++s.list_inits;
    s.poly_sq = s.poly_dma = 0;
}

void gfx_probe_start_render(const Sh4Context*) {
    State&          s = state();
    std::lock_guard lk(s.mu);
    ++s.renders;
    s.render_sq.add(uint32_t(s.poly_sq));
    s.render_dma.add(uint32_t(s.poly_dma));
    s.render_total.add(uint32_t(s.poly_sq + s.poly_dma));
    const uint32_t sof1 = pvr_reg(0x005F8060);
    ++s.targets[sof1];
    if (sof1 & 0x01000000) ++s.sof1_bit24;
    ++s.fb_w_ctrl[pvr_reg(0x005F8048)];
    ++s.fb_w_stride[pvr_reg(0x005F804C)];
    ++s.displayed[pvr_reg(0x005F8050)];
    ++s.displayed[pvr_reg(0x005F8054)];
}

void gfx_probe_texture(uint32_t tcw, uint32_t width, uint32_t height) {
    (void)width, (void)height;
    State&          s = state();
    std::lock_guard lk(s.mu);
    ++s.textures;
    // inicio da textura no layout de armazenamento (caminho de 32 bits) contra cada alvo de render nao exibido
    const uint32_t start = vram_offset64((tcw & 0x1FFFFF) << 3);
    for (const auto& [sof1, n] : s.targets) {
        if (s.displayed.count(sof1)) continue;
        const uint32_t base = sof1 & (kVramSize - 1);
        if (start >= base && start < base + 640 * 480 * 4) {
            ++s.rtt_hits[tcw];
            break;
        }
    }
}

bool gfx_probe_flush() {
    State&             s = state();
    std::lock_guard    lk(s.mu);
    if (s.path.empty()) return false;
    std::ostringstream o;
    o << "# N8.7 fase 0 (BS_HW_PROBE_GRAFICOS): fronteira grafica guest\n";
    o << "\n[area4] caminho\tfuncao\tregiao\tblocos_32B\n";
    for (const auto& [k, n] : s.ta) o << std::get<0>(k) << '\t' << hex(std::get<1>(k)) << '\t' << std::get<2>(k) << '\t' << n << '\n';
    o << "\n[dma_ch2] funcao\tregiao\ttransferencias\tbytes\tmin\tmax\n";
    for (const auto& [k, a] : s.dma)
        o << hex(k.first) << '\t' << k.second << '\t' << a.n << '\t' << a.bytes << '\t' << a.min << '\t' << a.max << '\n';
    o << "\n[render] renders\t" << s.renders << "\tlist_init\t" << s.list_inits << '\n';
    auto agg = [&](const char* n, const Agg& a) {
        o << n << "\tmedia " << (a.n ? a.bytes / a.n : 0) << "\tmin " << (a.n ? a.min : 0) << "\tmax " << a.max << '\n';
    };
    agg("bytes_poligono_sq", s.render_sq);
    agg("bytes_poligono_dma", s.render_dma);
    agg("bytes_poligono_total", s.render_total);
    o << "\n[alvos] FB_W_SOF1\trenders\texibido\n";
    for (const auto& [a, n] : s.targets) o << hex(a) << '\t' << n << '\t' << (s.displayed.count(a) ? "sim" : "NAO") << '\n';
    o << "FB_W_SOF1 com o bit 24\t" << s.sof1_bit24 << '\n';
    o << "[exibidos] FB_R_SOF1/2\tvezes\n";
    for (const auto& [a, n] : s.displayed) o << hex(a) << '\t' << n << '\n';
    o << "[FB_W_CTRL]";
    for (const auto& [a, n] : s.fb_w_ctrl) o << ' ' << hex(a) << 'x' << n;
    o << "\n[FB_W_LINESTRIDE]";
    for (const auto& [a, n] : s.fb_w_stride) o << ' ' << hex(a) << 'x' << n;
    o << "\n\n[render_para_textura] texturas amostradas " << s.textures << "; dentro de um alvo nao exibido: "
      << s.rtt_hits.size() << " TCW\n";
    for (const auto& [t, n] : s.rtt_hits) o << hex(t) << '\t' << n << '\n';
    o << "\n[vram_memoria] funcao\tcaminho\tescritas\n";
    for (const auto& [k, n] : s.vram) o << hex(k.first) << '\t' << k.second << '\t' << n << '\n';
    o << "\n[registradores] funcao\tregistrador\toperacao\tacessos\tvalores (amostra)\n";
    for (const auto& [k, u] : s.regs) {
        o << hex(std::get<0>(k)) << '\t' << hex(std::get<1>(k)) << '\t' << (std::get<2>(k) ? 'W' : 'R') << '\t' << u.n << '\t';
        for (uint32_t v : u.values) o << hex(v) << ' ';
        o << '\n';
    }
    o << "\n[tratador_kamui] chamadas " << s.isr_calls << "; sequencias distintas " << s.isr_seqs.size()
      << (s.isr_calls && sh4_override_get(kIsr) != isr_wrapper
              ? "; ATENCAO: outra substituicao tomou o lugar da sonda (sequencias incompletas)"
              : "")
      << '\n';
    std::vector<std::pair<uint64_t, std::string>> seqs;
    for (const auto& [q, n] : s.isr_seqs) seqs.push_back({n, q});
    std::sort(seqs.rbegin(), seqs.rend());
    for (size_t i = 0; i < seqs.size() && i < 40; ++i) o << seqs[i].first << '\t' << seqs[i].second << '\n';
    const std::string text = o.str();
    FILE*             f    = std::fopen(s.path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace bs
