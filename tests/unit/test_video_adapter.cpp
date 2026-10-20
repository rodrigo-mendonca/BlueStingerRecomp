// N5: contrato da cola guest <-> player nativo (transitional/video_mw_glue.h) com uma RAM sintetica e
// saidas de audio falsas. Valida o CONTRATO do adaptador (pool, tabela, handles alheios, ciclo de vida,
// erros, relogio); nao e prova de equivalencia com o Dreamcast (essa vem do oraculo e do test_video).
#include "test.h"

#include "video/mf_movie.h"
#include "video_mw_glue.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace nv = bs::native::video;
namespace na = bs::native::audio;
namespace tv = bs::transitional::video;

namespace {

constexpr uint32_t kPool = 0x8C2D0D80;  // valor do literal 8C1580B8 no executavel
constexpr int64_t  kMs   = 10'000;

// RAM do guest: palavras de 32 bits por endereco, strings e o relogio controlados pelo teste
struct FakeGuest final : tv::GuestPort {
    std::map<uint32_t, uint32_t>    mem;
    std::map<uint32_t, std::string> strs;
    int64_t                         now = 0;
    bool                            poolReadable = true;
    std::optional<uint32_t> load32(uint32_t a) override {
        if (a == tv::kMwPoolPtr && !poolReadable) return std::nullopt;
        const auto it = mem.find(a);
        return it == mem.end() ? 0u : it->second;
    }
    bool store32(uint32_t a, uint32_t v) override { mem[a] = v; return true; }
    std::optional<std::string> cstring(uint32_t a, size_t) override {
        const auto it = strs.find(a);
        return it == strs.end() ? std::optional<std::string>() : it->second;
    }
    int64_t now100ns() override { return now; }
    FakeGuest() { mem[tv::kMwPoolPtr] = kPool; }
    uint32_t entry(int i) const { return kPool + 4 + uint32_t(i) * tv::kMwEntrySize; }
};

struct CountingOut final : na::IAudioOutput {
    int*  closes;
    bool  failWrites;
    explicit CountingOut(int* c, bool f = false) : closes(c), failWrites(f) {}
    bs::native::Result<void> open(uint32_t, uint8_t) override { return {}; }
    size_t                   writable_frames() const override { return 100000; }
    bs::native::Result<void> write(std::span<const int16_t>) override { return failWrites ? bs::native::Result<void>(bs::native::Error::Io) : bs::native::Result<void>(); }
    void                     close() override { ++*closes; }
    void                     pause(bool) override {}
    void                     flush() override {}
    uint64_t                 played_frames() const override { return 0; }
    bool                     healthy() const override { return true; }
};

struct TempDir {
    fs::path p;
    explicit TempDir(const char* tag) {
        p = fs::temp_directory_path() / (std::string("bs-vad-") + tag + "-" + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(p, ec); }
};

struct Fixture {
    TempDir  d{"a"};
    fs::path data = d.p / "video";
    int      closes = 0;
    bool     failAudio = false;
    std::vector<uint32_t> sinkWidths;
    Fixture() {
        fs::create_directories(data);
        nv::MfMovieWriter w;
        w.open(data / "E001.mp4", {});
        std::vector<uint32_t> px(320 * 240, 0xFF303030u);
        for (int i = 0; i < 15; ++i) w.write_frame(px);  // 0,5 s
        std::vector<int16_t> pcm(22050 * 2, 1000);
        w.write_audio(pcm);
        w.finish();
        std::ofstream(data / "manifest.toml") << "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\n";
    }
    tv::MwAdapter make() {
        tv::MwCoreOptions o;
        o.catalog   = {data, {}};
        o.makeAudio = [this] { return std::make_unique<CountingOut>(&closes, failAudio); };
        return tv::MwAdapter(o, [this](const nv::VideoFrame& f) { sinkWidths.push_back(f.width); });
    }
};

uint32_t call(tv::MwAdapter& a, FakeGuest& g, uint32_t e, uint32_t r4 = 0, uint32_t r5 = 0) {
    const auto r = a.call(e, {r4, r5}, g);
    return r.forward ? 0xF0F0F0F0u : r.r0;  // marcador: a chamada iria para o original
}

int32_t playToEnd(tv::MwAdapter& a, FakeGuest& g, uint32_t h, int64_t from, int64_t step = 16) {
    int32_t st = 0;
    for (int64_t t = from; t <= from + 3000 && st != tv::kMwPlayEnd && st != tv::kMwError; t += step) {
        g.now = t * kMs;
        st    = int32_t(call(a, g, tv::kEntStat, h));
    }
    return st;
}

} // namespace

// create -> parametros -> start -> estado... -> fim natural -> stop -> destroy; pool e tabela no guest
BS_TEST(video_adapter_lifecycle) {
    Fixture   f;
    FakeGuest g;
    auto      a = f.make();
    g.mem[g.entry(0) + 4] = 1;  // entrada 0 ja em uso por outro player: o nativo pega a 1
    for (uint32_t off = 0; off < tv::kMwEntrySize; off += 4) g.mem[g.entry(1) + off] = 0xA5A5A5A5u;  // restos
    g.mem[g.entry(1) + 4] = 0;
    const uint32_t h = call(a, g, tv::kEntCreate, 0x8C300000);
    BS_CHECK(h == g.entry(1) && g.mem[h] == tv::kMwVtable && g.mem[h + 4] == 1);
    bool zero = true;  // +64 = 0: o servidor por handle do MW (8C1588D8) passa sem efeito
    for (uint32_t off = 8; off < tv::kMwEntrySize; off += 4) zero = zero && g.mem[h + off] == 0;
    BS_CHECK(zero);
    call(a, g, tv::kEntParam, h, 1), call(a, g, tv::kEntBase, h, 0xC0), call(a, g, tv::kEntOffset, h, 0x00FF);
    BS_CHECK(a.core().base() == 0xC0 && a.core().offset() == 0xFF);
    g.strs[0x8C0B83D8] = "E001.SFD";
    BS_CHECK(call(a, g, tv::kEntStart, h, 0x8C0B83D8) == 0 && a.playing());
    BS_CHECK(call(a, g, tv::kEntStat, h) == tv::kMwPlaying && !f.sinkWidths.empty() && f.sinkWidths[0] == 320);
    BS_CHECK(g.mem[h + 8] == uint32_t(tv::kMwPlaying));  // estado espelhado em +8
    BS_CHECK(playToEnd(a, g, h, 0) == tv::kMwPlayEnd && !a.playing());
    BS_CHECK(call(a, g, tv::kEntStop, h) == 0 && call(a, g, tv::kEntStat, h) == tv::kMwStop && g.mem[h + 8] == 0);
    BS_CHECK(a.stats().serverExposed == 0);
    BS_CHECK(call(a, g, tv::kEntDestroy, h) == 0 && g.mem[h + 4] == 0 && a.handle() == 0);
    BS_CHECK(f.closes >= 1);  // a saida de audio foi fechada (recursos liberados)
}

// stop antes do start; destroy no meio da reproducao; handle alheio vai para o original
BS_TEST(video_adapter_order_and_foreign) {
    Fixture   f;
    FakeGuest g;
    auto      a = f.make();
    const uint32_t h = call(a, g, tv::kEntCreate);
    BS_CHECK(call(a, g, tv::kEntStop, h) == 0 && call(a, g, tv::kEntStat, h) == tv::kMwStop);  // nada a parar
    g.strs[0x100] = "E001.SFD";
    call(a, g, tv::kEntStart, h, 0x100);
    call(a, g, tv::kEntStat, h);
    BS_CHECK(a.playing());
    // outro player (o WAV usa a mesma biblioteca): estado, stop, destroy e cor vao para o original
    const uint32_t other = g.entry(5);
    BS_CHECK(call(a, g, tv::kEntStat, other) == 0xF0F0F0F0u && call(a, g, tv::kEntStop, other) == 0xF0F0F0F0u);
    BS_CHECK(call(a, g, tv::kEntDestroy, other) == 0xF0F0F0F0u && call(a, g, tv::kEntBase, other, 1) == 0xF0F0F0F0u);
    BS_CHECK(call(a, g, tv::kEntParam, other, 1) == 0xF0F0F0F0u && call(a, g, tv::kEntParam, h, 1) == 0);  // parametro: idem
    BS_CHECK(a.stats().foreign == 3 && a.playing());  // o nativo seguiu tocando
    BS_CHECK(call(a, g, tv::kEntDestroy, h) == 0 && !a.playing() && g.mem[h + 4] == 0);
    BS_CHECK(call(a, g, tv::kEntStat, h) == 0xF0F0F0F0u);  // depois do destroy o handle nao e mais nosso
}

// falhas do create: pool cheio, literal do pool ilegivel, segundo player vivo
BS_TEST(video_adapter_create_failures) {
    Fixture   f;
    FakeGuest g;
    auto      a = f.make();
    for (int i = 0; i < 32; ++i) g.mem[g.entry(i) + 4] = 1;
    BS_CHECK(call(a, g, tv::kEntCreate) == 0);
    g.mem[g.entry(7) + 4] = 0;
    const uint32_t h = call(a, g, tv::kEntCreate);
    BS_CHECK(h == g.entry(7));
    g.mem[g.entry(8) + 4] = 0;
    BS_CHECK(call(a, g, tv::kEntCreate) == 0);  // um filme por vez
    call(a, g, tv::kEntDestroy, h);
    FakeGuest bad;
    bad.poolReadable = false;
    auto b = f.make();
    BS_CHECK(call(b, bad, tv::kEntCreate) == 0 && a.stats().createFailures == 2 && b.stats().createFailures == 1);
}

// filme ausente (estado de ausente, PENDENTE O1b: hipotese 4) e reinicio limpo depois da falha
BS_TEST(video_adapter_missing_then_restart) {
    Fixture   f;
    FakeGuest g;
    auto      a = f.make();
    const uint32_t h = call(a, g, tv::kEntCreate);
    g.strs[1] = "OPENING.SFD", g.strs[2] = "E001.SFD";
    call(a, g, tv::kEntStart, h, 1);
    BS_CHECK(call(a, g, tv::kEntStat, h) == tv::kMwError && !a.playing());
    call(a, g, tv::kEntStop, h);
    call(a, g, tv::kEntStart, h, 2);
    BS_CHECK(call(a, g, tv::kEntStat, h) == tv::kMwPlaying && playToEnd(a, g, h, 0) == tv::kMwPlayEnd);
    // nome ilegivel (ponteiro invalido): tratado como ausente
    call(a, g, tv::kEntStop, h);
    call(a, g, tv::kEntStart, h, 0xDEAD);
    BS_CHECK(call(a, g, tv::kEntStat, h) == tv::kMwError);
}

// relogio logico: parado repete o quadro; o filme so acaba pelo tempo logico, nunca pelo de parede
BS_TEST(video_adapter_logical_clock) {
    Fixture   f;
    FakeGuest g;
    auto      a = f.make();
    const uint32_t h = call(a, g, tv::kEntCreate);
    g.strs[1]        = "E001.SFD";
    g.now            = 5000 * kMs;
    call(a, g, tv::kEntStart, h, 1);
    for (int i = 0; i < 50; ++i) BS_CHECK(call(a, g, tv::kEntStat, h) == tv::kMwPlaying);  // relogio parado
    BS_CHECK(a.core().stats().framesShown == 1);
    g.now = 5250 * kMs;
    call(a, g, tv::kEntStat, h);
    BS_CHECK(a.core().stats().framesShown == 2 && a.core().stats().framesSkipped >= 5);  // salto de 0,25 s
    BS_CHECK(playToEnd(a, g, h, 5250) == tv::kMwPlayEnd);
}

// audio: a saida recusa os blocos -> saida nula, o filme termina; o proximo filme abre a saida de novo
BS_TEST(video_adapter_audio_failure_and_recovery) {
    Fixture f;
    f.failAudio = true;
    FakeGuest g;
    auto      a = f.make();
    const uint32_t h = call(a, g, tv::kEntCreate);
    g.strs[1]        = "E001.SFD";
    call(a, g, tv::kEntStart, h, 1);
    BS_CHECK(playToEnd(a, g, h, 0) == tv::kMwPlayEnd && a.core().stats().audioLost);
    f.failAudio = false;
    call(a, g, tv::kEntStop, h);
    const int closesBefore = f.closes;
    call(a, g, tv::kEntStart, h, 1);
    BS_CHECK(playToEnd(a, g, h, 10000) == tv::kMwPlayEnd);
    call(a, g, tv::kEntDestroy, h);
    BS_CHECK(f.closes > closesBefore);  // a saida nova tambem foi fechada no fim
}
