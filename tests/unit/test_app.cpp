// Executavel nativo sem janela: saida de video do PVR (runtime/pvr/pvr_video), teclado
// por eventos (runtime/input) e GameHost (app/game_host): inicializacao, quadros a cada
// VBlank, parada, erro e encerramento com fibras.
#include "test.h"

#include "boot/native_boot.h"
#include "cpu/sh4_threads.h"
#include "cpu/sh4_gen.h"
#include "cpu/sh4_mem.h"
#include "game_host.h"
#include "input/input.h"
#include "pvr/pvr_regs.h"
#include "pvr/pvr_video.h"
#include "pvr/vram.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace bs;

namespace {

void setReg(uint32_t off, uint32_t v) {
    std::vector<uint8_t> ram(16);
    Sh4Context           c{};
    c.ram = ram.data();
    sh4_write32(&c, 0xA05F8000 + off, v);
}

void put16(uint32_t addr, uint16_t v) { std::memcpy(vram_data() + addr, &v, 2); }

// 4 pixels de 16 bits por linha (2 palavras), `lines` linhas, modulo 1 (passo = largura).
void video(uint32_t ctrl, uint32_t lines, bool interlace, uint32_t mod = 1) {
    pvr_regs_reset();
    vram_reset();
    setReg(0x044, ctrl);
    setReg(0x05C, (2 - 1) | ((lines - 1) << 10) | (mod << 20));
    setReg(0x050, 0x1000);
    setReg(0x054, 0x2000);
    setReg(0x0D0, interlace ? 0x10 : 0);
}

} // namespace

// 565 sem entrelacamento: 4x2, passo de linha = largura.
BS_TEST(video_frame_565_progressive) {
    video(1 | (1u << 2), 2, false);
    put16(0x1000, 0xF800);      // (0,0) vermelho
    put16(0x1000 + 6, 0x07E0);  // (3,0) verde
    put16(0x1000 + 8, 0x001F);  // (0,1) azul
    std::vector<uint32_t> f;
    uint32_t              w, h;
    pvr_video_frame(f, w, h);
    BS_CHECK(w == 4 && h == 2);
    BS_CHECK(f[0] == 0xFFFF0000 && f[3] == 0xFF00FF00 && f[4] == 0xFF0000FF && f[1] == 0xFF000000);
}

// Entrelacado: linhas pares de FB_R_SOF1, impares de FB_R_SOF2; com modulo 3, o passo
// de cada campo pula uma linha (2 + 3 - 1 = 4 palavras = 16 bytes), como o jogo usa.
BS_TEST(video_frame_interlaced_weave) {
    video(1, 2, true, 3);  // 0555
    put16(0x1000, 0x7C00);       // campo 1, linha 0 -> y 0
    put16(0x1000 + 16, 0x03E0);  // campo 1, linha 1 -> y 2
    put16(0x2000, 0x001F);       // campo 2, linha 0 -> y 1
    std::vector<uint32_t> f;
    uint32_t              w, h;
    pvr_video_frame(f, w, h);
    BS_CHECK(w == 4 && h == 4);
    BS_CHECK(f[0] == 0xFFFF0000 && f[4] == 0xFF0000FF && f[8] == 0xFF00FF00);
}

// Duplicacao de linha (FB_R_CTRL bit 1), formatos de 24 e 32 bits, video desligado e blank.
BS_TEST(video_frame_line_double_formats_and_blank) {
    video(1 | 2 | (1u << 2), 1, false);
    put16(0x1000, 0xF800);
    std::vector<uint32_t> f;
    uint32_t              w, h;
    pvr_video_frame(f, w, h);
    BS_CHECK(h == 2 && f[0] == 0xFFFF0000 && f[4] == 0xFFFF0000);
    video(1 | (3u << 2), 1, false);  // 0888: 2 pixels por 2 palavras
    const uint8_t px[4] = {0x30, 0x20, 0x10, 0x99};  // B G R K
    std::memcpy(vram_data() + 0x1000, px, 4);
    pvr_video_frame(f, w, h);
    BS_CHECK(w == 2 && f[0] == 0xFF102030);
    video(1 | (2u << 2), 1, false);  // 888 empacotado: 8 bytes = 2 pixels (+ sobra)
    const uint8_t p3[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    std::memcpy(vram_data() + 0x1000, p3, 6);
    pvr_video_frame(f, w, h);
    BS_CHECK(f[0] == 0xFF030201 && f[1] == 0xFF060504);
    video(0 | (1u << 2), 1, false);  // leitura desligada
    put16(0x1000, 0xF800);
    pvr_video_frame(f, w, h);
    BS_CHECK(f[0] == 0xFF000000);
    video(1 | (1u << 2), 1, false);
    setReg(0x0E8, 8);  // VO_CONTROL: blank
    put16(0x1000, 0xF800);
    pvr_video_frame(f, w, h);
    BS_CHECK(f[0] == 0xFF000000);
}

// Teclado por eventos da janela: mapa padrao (ENTER = START, setas, Z = A), nomes sem
// diferenca de caixa, soltar e perder o foco.
BS_TEST(key_events_map_to_pad_state) {
    auto k = make_key_event_input();
    k->key("ENTER", true);
    k->key("left", true);
    BS_CHECK(k->read(0, 0).buttons == (kPadStart | kPadLeft));
    BS_CHECK(k->read(1, 0).buttons == 0);  // so a porta A
    k->key("ENTER", false);
    BS_CHECK(k->read(0, 1).buttons == kPadLeft);
    k->key("Q", true);  // gatilho L
    BS_CHECK(k->read(0, 2).ltrig == 255);
    k->key("F9", true);  // sem mapeamento: ignorada
    k->releaseAll();
    const PadState s = k->read(0, 3);
    BS_CHECK(s.buttons == 0 && s.ltrig == 0);
    auto custom = make_key_event_input("START = SPACE\nA = ENTER\n");
    custom->key("SPACE", true);
    custom->key("ENTER", true);
    BS_CHECK(custom->read(0, 0).buttons == (kPadStart | kPadA));
}

namespace {

// Guest falso: laco infinito que avanca o tempo e passa pelo poll (como o jogo).
void fakeEntry(Sh4Context* c) {
    for (;;) {
        c->cycles += 2000;
        BS_POLL(0xAC010000u);
    }
}
void fakeReturns(Sh4Context*) {}
void fakeFatal(Sh4Context* c) { sh4_fatal(c, "teste: erro fatal do guest", 0xAC010000u, 7); }

// Thread do guest em outra fibra: a entrada comeca uma thread nova (rte para outra pilha),
// que fica no laco; a parada e pedida enquanto ela roda.
void fakeThread(Sh4Context* c) { fakeEntry(c); }
void fakeWithThread(Sh4Context* c) {
    c->ret_expect = 0xAC010100;
    c->ret_sp     = c->r[15];
    c->r[15]      = 0x8C800000;
    sh4_rte_redirect(c, 0xAC010000, 0x8C020000);
    fakeEntry(c);
}

Sh4FuncEntry g_table[2];

GameOptions fakeOptions(Sh4Func entry, bool withThread = false) {
    GameOptions o;
    o.controls = ControlsProvider::Legacy;  // jogo falso: sem o SDK pd para o provedor nativo
    o.saves    = SavesProvider::Legacy;     // nem a bu
    o.video    = VideoProvider::Legacy;     // nem o MW (Sofdec) para os filmes nativos
    o.registerFunctions = [entry, withThread] {
        g_table[0] = {0x0C010000, entry};
        g_table[1] = {0x0C020000, fakeThread};
        sh4_set_function_table(g_table, withThread ? 2 : 1);
    };
    o.prepare = [](Sh4Context& c) {
        BootFiles f;
        f.ipBin.assign(0x8000, 0);
        f.firstRead.assign(64, 0);
        boot_prepare(c, f, 0);  // estado do BIOS: PVR e SPG configurados (VBlank anda)
        const uint32_t v = 0x00F8F8F8;  // FB_R_SOF1 = 0x200000, 0888: um pixel claro
        std::memcpy(vram_data() + 0x200000, &v, 4);
    };
    o.realtime = false;
    return o;
}

} // namespace

// Inicializacao + apresentacao: o host publica o quadro exibido a cada VBlank e para no
// limite pedido, sem erro.
BS_TEST(game_host_publishes_frames_and_stops_at_limit) {
    GameHost    h;
    GameOptions o   = fakeOptions(fakeEntry);
    o.vblankLimit   = 5;
    h.start(o);
    h.join();
    BS_CHECK(!h.running() && h.error().empty() && h.vblanks() == 5);
    const GameStats st = h.stats();
    BS_CHECK(st.vblanks == 5 && st.workAvgMs >= 0 && st.workMaxMs >= st.workAvgMs);
    GameFrame f;
    uint64_t  seq = 0;
    BS_CHECK(h.latestFrame(f, seq) && seq == 5);
    // FB_R_SIZE do BIOS (0x2813BE7F): 640 palavras de 0888 e 240 linhas por campo, entrelacado
    BS_CHECK(f.width == 640 && f.height == 480);
    BS_CHECK(f.argb[0] == 0xFFF8F8F8);
    BS_CHECK(!h.latestFrame(f, seq));  // nada novo
}

// Diagnostico BS_RAM_WATCH mal configurado vira erro da sessao (nao std::terminate na thread do host) e o
// estado global e limpo: a sessao seguinte comeca normal (revisao Codex 2026-10-20)
BS_TEST(game_host_ram_watch_bad_config_is_session_error) {
    _putenv_s("BS_RAM_WATCH", "sem-dois-pontos");
    GameHost    h;
    GameOptions o = fakeOptions(fakeEntry);
    o.vblankLimit = 2;
    h.start(o);
    h.join();
    _putenv_s("BS_RAM_WATCH", "");
    BS_CHECK(!h.running() && h.error().find("BS_RAM_WATCH") != std::string::npos);
    GameHost h2;
    h2.start(o);
    h2.join();
    BS_CHECK(h2.error().empty() && h2.vblanks() == 2);
}

// Encerramento pedido de fora (janela fechada), com uma thread do guest numa fibra: a
// parada atravessa as fibras, volta a thread 0 e as fibras sao liberadas.
BS_TEST(game_host_stop_request_with_guest_thread) {
    const int fibersBefore = sh4_live_fibers();
    GameHost  h;
    h.start(fakeOptions(fakeWithThread, true));
    const auto t0 = std::chrono::steady_clock::now();
    while (h.vblanks() < 3 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    BS_CHECK(h.vblanks() >= 3 && h.running());
    BS_CHECK(sh4_live_fibers() == fibersBefore + 1);  // a thread do guest roda numa fibra
    h.requestStop();
    h.join();
    BS_CHECK(!h.running() && h.error().empty());
    BS_CHECK(sh4_live_fibers() == fibersBefore);  // encerramento apagou a fibra
    // um segundo host (outra thread do sistema) comeca do zero
    GameHost    h2;
    GameOptions o = fakeOptions(fakeEntry);
    o.vblankLimit = 2;
    h2.start(o);
    h2.join();
    BS_CHECK(h2.error().empty() && h2.vblanks() == 2);
}

// Segunda sessao no mesmo processo: boot_prepare volta todo o hardware ao reset. Sem isso,
// a VRAM suja ficava e o SPG, com o tempo da sessao anterior (ciclos muito altos), nao
// gerava VBlank ate o novo contador passar dele (revisao Codex 2026-10-14).
BS_TEST(boot_prepare_resets_hardware_between_sessions) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    BootFiles f;
    f.ipBin.assign(0x8000, 0);
    f.firstRead.assign(64, 0);
    boot_prepare(c, f, 0);
    vram_data()[0x300000] = 0xAB;          // sessao anterior: VRAM suja
    c.cycles              = 50000000000ull;  // e o SPG muito adiante
    sh4_read32(&c, 0xA05F6900);
    Sh4Context c2{};
    c2.ram = ram.data();
    boot_prepare(c2, f, 0);
    BS_CHECK(vram_data()[0x300000] == 0);
    sh4_write32(&c2, 0xA05F6900, 0xFFFFFFFF);  // limpa ISTNRM
    c2.cycles = 7000000;                       // ~2 campos de 3,3 milhoes de ciclos
    BS_CHECK((sh4_read32(&c2, 0xA05F6900) & (1u << 3)) != 0);  // VBlank-in na nova sessao
}

// Erros: a entrada retorna, ou o guest para com erro fatal.
BS_TEST(game_host_reports_errors) {
    {
        GameHost h;
        h.start(fakeOptions(fakeReturns));
        h.join();
        BS_CHECK(h.error().find("retornou") != std::string::npos);
    }
    {
        GameHost h;
        h.start(fakeOptions(fakeFatal));
        h.join();
        BS_CHECK(h.error().find("teste: erro fatal") != std::string::npos);
    }
}
