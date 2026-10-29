#pragma once
// N8.7 (docs/N8-7-KAMUI.md secoes 5 e 12): o estado do quadro que o Kamui programa nos registradores graficos
// (005F8000-005F9FFF), como um servico nativo, sem o hardware simulado. Guarda o que o jogo grava, responde o que ele le
// (ID, REVISION, SPG_STATUS, TA_ITP_CURRENT...) e transforma as escritas de comando em efeitos (inicio do render, listas
// novas ou continuadas, temporizacao do video), executados pelo chamador na ordem de hoje.
//
// A temporizacao do video (SPG) e a mesma conta do modelo anterior: o campo dura `field_ticks` do relogio, dividido
// igualmente entre as linhas; a fase do feixe vem do SPG_STATUS deixado pelo boot. Nenhum relogio muda (D-K4).
#include <array>
#include <cstdint>

namespace bs::native::render::kamui {

// Os efeitos das escritas de comando, na ordem do modelo anterior: start_render, spg_sync, list_init, list_continue,
// (o valor e guardado), spg_changed, yuv.
class FrameEffects {
public:
    virtual ~FrameEffects()                       = default;
    virtual void start_render()                   = 0;  // STARTRENDER (antes de qualquer outra coisa)
    virtual void spg_sync()                       = 0;  // SPG_LOAD/SPG_VBLANK_INT: antes do valor novo
    virtual void list_init()                      = 0;  // TA_LIST_INIT com o bit 31
    virtual void list_continue()                  = 0;  // TA_LIST_CONT (qualquer escrita)
    virtual void spg_changed()                    = 0;  // SPG_LOAD/SPG_VBLANK_INT: depois do valor novo
    virtual void yuv(uint32_t base, uint32_t ctrl) = 0;  // TA_YUV_TEX_BASE/CTRL (o produto recusa: N8.5)
};

struct SpgPosition {
    uint32_t line  = 0;  // 0 = inicio do vsync
    bool     vsync = false;
    bool     blank = false;
};

// O custo do render ate o fim (ISTNRM bits 0-2): a estimativa do Flycast, min(450000 + bytes do TA * 100, 1500000)
// ciclos, na base do relogio. O hardware nao documenta a duracao; e o contrato das referencias (D-K4).
inline uint64_t render_done_cost(uint32_t ta_bytes) {
    const uint64_t c = 450000 + uint64_t(ta_bytes) * 100;
    return c < 1500000 ? c : 1500000;
}

class FrameState {
public:
    static constexpr uint32_t kBase = 0x005F8000, kSize = 0x2000;
    static constexpr uint64_t kNever = ~0ull;

    explicit FrameState(uint64_t field_ticks) : field_(field_ticks) {}

    void reset();
    // o banco inteiro deixado pelo boot (ID, REVISION e SPG_STATUS sao calculados); fixa a fase do feixe
    void load(const uint32_t* words);

    static bool contains(uint32_t p) { return p >= kBase && p < kBase + kSize; }
    // leitura pelo codigo do jogo (endereco fisico, 32 bits). `modeled` = false para um registrador sem semantica propria
    // (o valor e o ultimo gravado: o chamador avisa uma vez)
    uint32_t read(uint32_t p, uint64_t now, bool& modeled) const;
    // escrita pelo codigo do jogo; os efeitos vao para `fx`, na ordem do modelo anterior
    void write(uint32_t p, uint32_t v, FrameEffects& fx);

    uint32_t reg(uint32_t p) const { return regs_[(p - kBase) / 4]; }  // o valor guardado (sem efeito)
    uint32_t palette_argb(uint32_t index) const;                         // entrada 0..1023 em ARGB8888 (PAL_RAM_CTRL)
    uint64_t palette_generation() const { return pal_gen_; }             // sobe a cada escrita na paleta ou no PAL_RAM_CTRL

    // temporizacao do video
    SpgPosition spg_position(uint64_t t) const;
    bool        spg_crossed(uint64_t from, uint64_t to, uint32_t line) const;
    uint64_t    spg_next_crossing(uint64_t after, uint32_t line) const;
    uint64_t    spg_next_line_change(uint64_t after) const;
    uint32_t    vblank_in_line() const { return reg(kSpgVblankInt) & 0x3FF; }
    uint32_t    vblank_out_line() const { return (reg(kSpgVblankInt) >> 16) & 0x3FF; }

    static constexpr uint32_t kId = 0x005F8000, kRevision = 0x005F8004, kSoftReset = 0x005F8008;
    static constexpr uint32_t kStartRender = 0x005F8014, kSpgVblankInt = 0x005F80CC, kSpgControl = 0x005F80D0;
    static constexpr uint32_t kSpgLoad = 0x005F80D8, kSpgVblank = 0x005F80DC, kSpgStatus = 0x005F810C;
    static constexpr uint32_t kTaIspBase = 0x005F8128, kTaNextOpb = 0x005F8134, kTaItpCurrent = 0x005F8138;
    static constexpr uint32_t kTaListInit = 0x005F8144, kTaYuvTexBase = 0x005F8148, kTaYuvTexCtrl = 0x005F814C;
    static constexpr uint32_t kTaYuvTexCnt = 0x005F8150, kTaListCont = 0x005F8160, kTaNextOpbInit = 0x005F8164;
    static constexpr uint32_t kPalRamCtrl = 0x005F8108, kPalette = 0x005F9000;

private:
    std::array<uint32_t, kSize / 4> regs_{};
    uint64_t                        field_;
    uint64_t                        phase_ = 0;  // fase do feixe no instante 0 (dentro de dois campos)
    uint64_t                        pal_gen_ = 0;
    uint32_t field_lines() const;
};

} // namespace bs::native::render::kamui
