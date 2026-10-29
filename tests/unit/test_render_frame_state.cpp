// N8.7 (docs/N8-7-KAMUI.md, incremento 3a): o estado do quadro nativo (native/render/kamui/frame_state), sem o jogo e
// sem o banco de hardware: valores lidos, comandos que nao guardam o valor, ponteiros do TA no TA_LIST_INIT, ordem dos
// efeitos, paleta e temporizacao do video.
#include "test.h"

#include "render/kamui/frame_state.h"

#include <string>
#include <vector>

using bs::native::render::kamui::FrameEffects;
using bs::native::render::kamui::FrameState;

namespace {

constexpr uint64_t kField = 3336814;  // um campo (o valor nao importa para as contas relativas)

class Log final : public FrameEffects {
public:
    std::vector<std::string> ev;
    void start_render() override { ev.push_back("render"); }
    void spg_sync() override { ev.push_back("sync"); }
    void list_init() override { ev.push_back("init"); }
    void list_continue() override { ev.push_back("cont"); }
    void spg_changed() override { ev.push_back("spg"); }
    void yuv(uint32_t b, uint32_t c) override { ev.push_back("yuv " + std::to_string(b) + " " + std::to_string(c)); }
};

} // namespace

BS_TEST(frame_state_identity_registers_and_unmodeled_reads) {
    FrameState fs(kField);
    bool       modeled = false;
    BS_CHECK(fs.read(FrameState::kId, 0, modeled) == 0x17FD11DB && modeled);
    BS_CHECK(fs.read(FrameState::kRevision, 0, modeled) == 0x11 && modeled);
    Log log;
    fs.write(0x005F80B0, 0x00123456, log);  // FOG_COL_RAM: dado simples
    BS_CHECK(fs.read(0x005F80B0, 0, modeled) == 0x00123456 && !modeled);
    BS_CHECK(log.ev.empty());
}

BS_TEST(frame_state_commands_do_not_store_and_effects_keep_the_order) {
    FrameState fs(kField);
    Log        log;
    fs.write(FrameState::kStartRender, 0xFFFFFFFF, log);
    BS_CHECK(fs.reg(FrameState::kStartRender) == 0 && log.ev.size() == 1 && log.ev[0] == "render");
    log.ev.clear();
    fs.write(FrameState::kSpgLoad, 0x020C0359, log);
    BS_CHECK(log.ev.size() == 2 && log.ev[0] == "sync" && log.ev[1] == "spg" && fs.reg(FrameState::kSpgLoad) == 0x020C0359);
    log.ev.clear();
    fs.write(FrameState::kTaListCont, 0x80000000, log);
    BS_CHECK(log.ev.size() == 1 && log.ev[0] == "cont");
    log.ev.clear();
    fs.write(FrameState::kTaYuvTexBase, 0x100, log);
    BS_CHECK(log.ev.size() == 1 && log.ev[0] == "yuv 256 0");
}

BS_TEST(frame_state_list_init_resets_the_ta_pointers) {
    FrameState fs(kField);
    Log        log;
    fs.write(FrameState::kTaIspBase, 0x00400000, log);
    fs.write(FrameState::kTaNextOpbInit, 0x00500000, log);
    fs.write(FrameState::kTaListInit, 0x00000000, log);  // sem o bit 31: comando sem efeito
    BS_CHECK(fs.reg(FrameState::kTaItpCurrent) == 0 && log.ev.empty());
    fs.write(FrameState::kTaListInit, 0x80000000, log);
    BS_CHECK(log.ev.size() == 1 && log.ev[0] == "init");
    BS_CHECK(fs.reg(FrameState::kTaItpCurrent) == 0x00400000 && fs.reg(FrameState::kTaNextOpb) == 0x00500000);
    BS_CHECK(fs.reg(FrameState::kTaListInit) == 0);  // comando: o valor nao fica
}

BS_TEST(frame_state_palette_formats) {
    FrameState fs(kField);
    Log        log;
    fs.write(FrameState::kPalette + 4, 0xF800, log);  // entrada 1
    fs.write(FrameState::kPalRamCtrl, 1, log);        // RGB565
    BS_CHECK(fs.palette_argb(1) == 0xFFFF0000);
    fs.write(FrameState::kPalRamCtrl, 0, log);  // ARGB1555: 0xF800 = alfa 1, vermelho 0x1E
    BS_CHECK(fs.palette_argb(1) == 0xFFF70000);
    fs.write(FrameState::kPalRamCtrl, 3, log);
    BS_CHECK(fs.palette_argb(1) == 0xF800);
}

BS_TEST(frame_state_spg_status_follows_the_beam_and_the_boot_phase) {
    FrameState fs(kField);
    Log        log;
    fs.write(FrameState::kSpgLoad, (262u << 16), log);                 // 263 linhas por campo
    fs.write(FrameState::kSpgVblank, (21u << 16) | 260u, log);         // vsync da linha 260 ate a 21
    bool modeled = false;
    BS_CHECK((fs.read(FrameState::kSpgStatus, 0, modeled) & 0x3FF) == 0);
    const uint64_t line100 = 100 * kField / 263 + 1;
    const uint32_t s       = fs.read(FrameState::kSpgStatus, line100, modeled);
    BS_CHECK((s & 0x3FF) == 100 && !(s & (1u << 13)));
    BS_CHECK(fs.read(FrameState::kSpgStatus, 0, modeled) & (1u << 13));  // linha 0: dentro do vsync (260..21)
    // a fase do boot: SPG_STATUS deixado na linha 100 = o instante 0 ja esta na linha 100
    std::vector<uint32_t> bank(FrameState::kSize / 4, 0);
    bank[(FrameState::kSpgLoad - FrameState::kBase) / 4]   = 262u << 16;
    bank[(FrameState::kSpgVblank - FrameState::kBase) / 4] = (21u << 16) | 260u;
    bank[(FrameState::kSpgStatus - FrameState::kBase) / 4] = 100;
    fs.load(bank.data());
    BS_CHECK((fs.read(FrameState::kSpgStatus, 0, modeled) & 0x3FF) == 100);
    BS_CHECK(fs.spg_next_crossing(0, 260) > 0 && fs.spg_crossed(0, fs.spg_next_crossing(0, 260), 260));
}
