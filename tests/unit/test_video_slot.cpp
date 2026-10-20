// N5: os slots reais das entradas MW (transitional/video_adapter.cpp) com Sh4Context, registro de
// fronteiras e camada de mods, sem o codigo gerado: uma tabela de funcoes falsa com as 8 entradas,
// originais que contam chamadas (o "Sofdec legado") e a RAM de 16 MB com o pool do MW.
#include "test.h"

#include "boundary_registry.h"
#include "cpu/sh4_runtime.h"
#include "mods/mod_api.h"
#include "video/mf_movie.h"
#include "video_adapter.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace nv = bs::native::video;
namespace tv = bs::transitional::video;
using bs::Sh4Context;
using bs::Sh4Func;

namespace {

constexpr uint32_t kPool = 0x8C2D0D80;
constexpr std::array<uint32_t, 8> kEntries = {tv::kEntCreate, tv::kEntParam, tv::kEntBase, tv::kEntOffset,
                                             tv::kEntDestroy, tv::kEntStart, tv::kEntStop, tv::kEntStat};

// "Sofdec legado": cada original so conta a chamada e devolve um marcador
std::array<int, 8> g_orig{};
template <int I>
void orig(Sh4Context* c) {
    ++g_orig[I];
    c->r[0] = 0x0BAD0000u + I;
}
Sh4Func g_slot[8] = {&orig<0>, &orig<1>, &orig<2>, &orig<3>, &orig<4>, &orig<5>, &orig<6>, &orig<7>};
const bs::mods::Slot kSlots[8] = {
    {tv::kEntCreate, &g_slot[0], &orig<0>}, {tv::kEntParam, &g_slot[1], &orig<1>}, {tv::kEntBase, &g_slot[2], &orig<2>},
    {tv::kEntOffset, &g_slot[3], &orig<3>}, {tv::kEntDestroy, &g_slot[4], &orig<4>}, {tv::kEntStart, &g_slot[5], &orig<5>},
    {tv::kEntStop, &g_slot[6], &orig<6>},   {tv::kEntStat, &g_slot[7], &orig<7>}};
bs::Sh4FuncEntry g_table[8];

uint32_t& word(std::vector<uint8_t>& ram, uint32_t a) { return *reinterpret_cast<uint32_t*>(&ram[a & 0x00FFFFFF]); }

struct Env {
    fs::path             dir = fs::temp_directory_path() / ("bs-vslot-" + std::to_string(std::random_device{}()));
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    bs::transitional::BoundaryRegistry reg;
    Env() {
        fs::create_directories(dir / "video");
        nv::MfMovieWriter w;
        w.open(dir / "video" / "E001.mp4", {});
        std::vector<uint32_t> px(320 * 240, 0xFF505050u);
        for (int i = 0; i < 15; ++i) w.write_frame(px);  // 0,5 s
        std::vector<int16_t> pcm(22050 * 2, 500);
        w.write_audio(pcm);
        w.finish();
        std::ofstream(dir / "video" / "manifest.toml") << "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\n";
        // tabela ordenada por endereco fisico
        std::array<uint32_t, 8> sorted = kEntries;
        std::sort(sorted.begin(), sorted.end());
        for (size_t i = 0; i < 8; ++i) {
            const auto it = std::find(kEntries.begin(), kEntries.end(), sorted[i]);
            g_table[i]    = {sorted[i] & 0x1FFFFFFF, kSlots[it - kEntries.begin()].original};
        }
        bs::sh4_set_function_table(g_table, 8);
        bs::mods::set_slots(kSlots, 8);
        g_orig = {};
        c.ram  = ram.data();
        word(ram, tv::kMwPoolPtr) = kPool;
        std::memcpy(&ram[0x00300000], "E001.SFD", 9);
    }
    ~Env() {
        tv::uninstall(reg);
        bs::mods::set_slots(nullptr, 0);
        bs::sh4_set_function_table(nullptr, 0);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    // chamada pelo slot, como o codigo gerado (direta) faria
    uint32_t call(uint32_t entry, uint32_t r4 = 0, uint32_t r5 = 0) {
        const auto i = std::find(kEntries.begin(), kEntries.end(), entry) - kEntries.begin();
        c.r[4] = r4, c.r[5] = r5, c.r[0] = 0xFFFFFFFFu;
        g_slot[i](&c);
        return c.r[0];
    }
    void at_ms(uint64_t ms) { c.cycles = ms * 200'000; }  // relogio do legado: ciclos de 200 MHz
    int  sofdec() const { int n = 0; for (int v : g_orig) n += v; return n; }
};

} // namespace

// instalacao: os 8 slots passam ao nativo; sem slot (build sem [native] slots) a instalacao recusa
BS_TEST(video_slot_install_and_refusal) {
    Env e;
    uint32_t frames = 0;
    tv::MwCoreOptions o;
    o.catalog = {e.dir / "video", {}};
    tv::install(e.reg, o, [&](const nv::VideoFrame&) { ++frames; });
    BS_CHECK(tv::installed() && e.reg.count(bs::transitional::Provider::Native) == 8);
    for (Sh4Func s : g_slot) BS_CHECK(s != &orig<0> && s != &orig<7>);
    tv::uninstall(e.reg);
    BS_CHECK(!tv::installed() && g_slot[0] == &orig<0> && g_slot[7] == &orig<7>);  // de volta ao gerado
    bs::mods::set_slots(nullptr, 0);
    bool refused = false;
    try {
        tv::install(e.reg, o, nullptr);
    } catch (const std::exception&) { refused = true; }
    BS_CHECK(refused && !tv::installed());
    bs::mods::set_slots(kSlots, 8);
}

// reproducao completa pelo slot: nenhuma chamada ao Sofdec legado com o handle nativo; retorno ao
// jogo pelos mesmos codigos (estado 3, stop e destroy devolvem 0); handle alheio vai ao original
BS_TEST(video_slot_playback_without_sofdec) {
    Env e;
    uint32_t frames = 0;
    tv::MwCoreOptions o;
    o.catalog = {e.dir / "video", {}};
    tv::install(e.reg, o, [&](const nv::VideoFrame& f) { frames += f.width == 320; });
    e.at_ms(1000);
    const uint32_t h = e.call(tv::kEntCreate, 0x8C300100);
    BS_CHECK(h == kPool + 4 && word(e.ram, h) == tv::kMwVtable && word(e.ram, h + 4) == 1);
    e.call(tv::kEntParam, h, 1), e.call(tv::kEntBase, h, 0xC0), e.call(tv::kEntOffset, h, 0);
    e.call(tv::kEntStart, h, 0x8C300000);
    uint32_t st = 0;
    for (uint64_t ms = 1000; ms < 3000 && st != tv::kMwPlayEnd; ms += 16) {
        e.at_ms(ms);
        st = e.call(tv::kEntStat, h);
        BS_CHECK(st == tv::kMwPlaying || st == tv::kMwPlayEnd);
        if (st == tv::kMwPlaying) BS_CHECK(tv::playing());
    }
    BS_CHECK(st == tv::kMwPlayEnd && !tv::playing() && frames >= 14 && word(e.ram, h + 8) == tv::kMwPlayEnd);
    BS_CHECK(e.call(tv::kEntStop, h) == 0 && e.call(tv::kEntDestroy, h) == 0 && word(e.ram, h + 4) == 0);
    BS_CHECK(e.sofdec() == 0);  // nenhum original do MW rodou durante o filme nativo
    // outro player (mesma biblioteca): o original atende
    BS_CHECK(e.call(tv::kEntStat, kPool + 4 + 120) == 0x0BAD0007u && g_orig[7] == 1);
    BS_CHECK(tv::summary().find("outros players 1, entrada exposta ao servidor 0") != std::string::npos);
    // o servidor por handle (8C1588D8) ignora a entrada com +64 = 0; um +64 deixado por alguem e acusado
    const uint32_t h2 = e.call(tv::kEntCreate);
    e.call(tv::kEntStart, h2, 0x8C300000);
    word(e.ram, h2 + 64) = 0x8C400000;
    e.call(tv::kEntStat, h2);
    BS_CHECK(tv::summary().find("entrada exposta ao servidor 1") != std::string::npos);
    e.call(tv::kEntDestroy, h2);
}

// filme ausente pelo slot: o estado de ausente (PENDENTE O1b) e o jogo segue com stop/destroy
BS_TEST(video_slot_missing_movie) {
    Env e;
    tv::MwCoreOptions o;
    o.catalog = {e.dir / "video", {}};
    tv::install(e.reg, o, nullptr);
    std::memcpy(&e.ram[0x00300000], "NEWGAME.SFD", 12);
    const uint32_t h = e.call(tv::kEntCreate);
    e.call(tv::kEntStart, h, 0x8C300000);
    BS_CHECK(e.call(tv::kEntStat, h) == uint32_t(o.missingStat) && !tv::playing());
    BS_CHECK(e.call(tv::kEntStop, h) == 0 && e.call(tv::kEntDestroy, h) == 0 && e.sofdec() == 0);
}
