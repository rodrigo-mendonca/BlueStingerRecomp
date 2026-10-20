// N5: nucleo do provedor nativo das entradas MW (transitional/video_mw_core.h). So filmes sinteticos
// (gravados aqui pelo Media Foundation), sem GPU e sem dispositivo de audio (saidas falsas).
// O que depende do oraculo O1b (preparacao, regra de fim, cor, ausente) e testado como PARAMETRO.
#include "test.h"

#include "video/mf_movie.h"
#include "video_mw_core.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace nv = bs::native::video;
namespace na = bs::native::audio;
namespace tv = bs::transitional::video;

namespace {

struct TempDir {
    fs::path p;
    explicit TempDir(const char* tag) {
        p = fs::temp_directory_path() / (std::string("bs-mw-") + tag + "-" + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(p, ec); }
};

void writeText(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }

// filme sintetico: `frames` quadros de cor solida (vermelho = indice*8), tom de audio opcional
void makeMovie(const fs::path& f, uint32_t w, uint32_t h, uint32_t fps, int frames, bool audio = true) {
    nv::MfMovieWriter mw;
    nv::WriterOptions o;
    o.width = w, o.height = h, o.fpsNum = fps, o.audio = audio;
    std::string err;
    if (!mw.open(f, o, &err)) std::printf("  (sem MF: %s)\n", err.c_str());
    std::vector<uint32_t> px(size_t(w) * h);
    for (int i = 0; i < frames; ++i) {
        std::fill(px.begin(), px.end(), 0xFF000000u | uint32_t((i * 8) & 0xFF) << 16);
        mw.write_frame(px);
    }
    if (audio) {
        std::vector<int16_t> pcm(size_t(44100) * frames / fps * 2, 3000);
        mw.write_audio(pcm);
    }
    mw.finish();
}

// saida de audio falsa: perde o dispositivo depois de N escritas, ou nunca tem espaco
struct FakeOut final : na::IAudioOutput {
    int    writes = 0, loseAfter = -1;
    bool   full = false, paused = false;
    size_t flushes = 0;
    bs::native::Result<void> open(uint32_t, uint8_t) override { return {}; }
    size_t                   writable_frames() const override { return full ? 0 : 8820; }
    bs::native::Result<void> write(std::span<const int16_t>) override { ++writes; return {}; }
    void                     close() override {}
    void                     pause(bool p) override { paused = p; }
    void                     flush() override { ++flushes; }
    uint64_t                 played_frames() const override { return 0; }
    bool                     healthy() const override { return loseAfter < 0 || writes < loseAfter; }
};

// catalogo com um filme do "Builder" (E001.SFD) e pasta de mods vazia
struct Fixture {
    TempDir  d{"core"};
    fs::path data = d.p / "data" / "video", mods = d.p / "mods" / "video";
    Fixture() {
        fs::create_directories(data), fs::create_directories(mods);
        makeMovie(data / "E001.mp4", 320, 240, 30, 30);
        writeText(data / "manifest.toml",
                  "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\nquadros = 30\nfps = 30.0\n"
                  "[[filme]]\nid = 0\noriginal = \"OPENING.SFD\"\nausente = true\n");
    }
    tv::MwCoreOptions opts() {
        tv::MwCoreOptions o;
        o.catalog = {data, mods};
        return o;
    }
};

constexpr int64_t kMs = 10'000;  // 1 ms em 100 ns

} // namespace

// create -> start -> status ... -> fim natural -> stop: estados 1, 2, 3; quadros pelo relogio logico
BS_TEST(video_mw_core_lifecycle) {
    Fixture         f;
    tv::MwPlayerCore c(f.opts());
    BS_CHECK(c.start("\\BSDATA\\E001.SFD", 1000 * kMs) == tv::kMwPrep);  // com pasta, como o jogo pode passar
    BS_CHECK(c.entry() && c.entry()->id == 3 && !c.entry()->mod);
    BS_CHECK(c.status(1000 * kMs) == tv::kMwPlaying);  // prepPolls = 1
    nv::VideoFrame fr;
    BS_CHECK(c.frame(fr) && fr.width == 320 && fr.height == 240);
    BS_CHECK(c.status(1000 * kMs) == tv::kMwPlaying && c.stats().framesShown == 1);  // relogio parado: mesmo quadro
    BS_CHECK(c.status(1500 * kMs) == tv::kMwPlaying);
    BS_CHECK(c.frame(fr) && std::abs(int((fr.argb[0] >> 16) & 0xFF) - 15 * 8) <= 12);  // 0,5 s = quadro 15
    int32_t st = 0;
    for (int64_t t = 1500; t <= 3000 && st != tv::kMwPlayEnd; t += 16) st = c.status(t * kMs);
    BS_CHECK(st == tv::kMwPlayEnd && c.status(4000 * kMs) == tv::kMwPlayEnd);  // fim e estavel
    c.stop();
    BS_CHECK(c.stats().starts == 1 && c.stats().stops == 1 && !c.frame(fr));
}

// preparacao parametrizada (PENDENTE O1b): N consultas em 1 antes do 2; o filme conta do fim dela
BS_TEST(video_mw_core_prep_polls) {
    Fixture f;
    auto    o  = f.opts();
    o.prepPolls = 3;
    tv::MwPlayerCore c(o);
    BS_CHECK(c.start("E001.SFD", 0) == tv::kMwPrep && c.status(0) == tv::kMwPrep && c.status(16 * kMs) == tv::kMwPrep);
    BS_CHECK(c.status(33 * kMs) == tv::kMwPlaying);
    nv::VideoFrame fr;
    BS_CHECK(c.frame(fr) && ((fr.argb[0] >> 16) & 0xFF) <= 12);  // quadro 0 no inicio da reproducao
}

// ausente no disco (OPENING) e nao convertido: estado de filme ausente (PENDENTE O1b: hipotese 4);
// um mod pelo nome do original fornece o OPENING sem recompilar
BS_TEST(video_mw_core_missing_and_mod) {
    Fixture     f;
    std::string log;
    auto        o = f.opts();
    o.log         = [&](const std::string& m) { log += m + "\n"; };
    {
        tv::MwPlayerCore c(o);
        BS_CHECK(c.start("OPENING.SFD", 0) == tv::kMwError && c.status(0) == tv::kMwError);
        BS_CHECK(log.find("ausente no disco original") != std::string::npos);
        BS_CHECK(c.start("E999.SFD", 0) == tv::kMwError && log.find("nao convertido") != std::string::npos);
    }
    o.missingStat = tv::kMwPlayEnd;  // o parametro e respeitado
    {
        tv::MwPlayerCore c(o);
        BS_CHECK(c.start("OPENING.SFD", 0) == tv::kMwPlayEnd);
    }
    makeMovie(f.mods / "OPENING.mp4", 1280, 720, 24, 12);  // mod HD a 24 fps
    tv::MwPlayerCore c(f.opts());
    BS_CHECK(c.start("OPENING.SFD", 0) == tv::kMwPrep && c.entry()->mod);
    int32_t st = c.status(0);
    nv::VideoFrame fr;
    BS_CHECK(st == tv::kMwPlaying && c.frame(fr) && fr.width == 1280 && fr.height == 720);
    for (int64_t t = 0; t <= 2000 && st != tv::kMwPlayEnd; t += 16) st = c.status(t * kMs);
    BS_CHECK(st == tv::kMwPlayEnd && c.stats().framesShown == 12);  // 0,5 s, duracao do proprio mod
}

// interrupcao (o jogo chama stop no meio, p.ex. START permitido) e restart limpo
BS_TEST(video_mw_core_interrupt_and_restart) {
    Fixture         f;
    tv::MwPlayerCore c(f.opts());
    c.start("E001.SFD", 0);
    c.status(0), c.status(200 * kMs);
    c.stop();
    BS_CHECK(c.state() == tv::kMwStop);
    BS_CHECK(c.start("E001.SFD", 5000 * kMs) == tv::kMwPrep && c.status(5000 * kMs) == tv::kMwPlaying);
    nv::VideoFrame fr;
    BS_CHECK(c.frame(fr) && ((fr.argb[0] >> 16) & 0xFF) <= 12);  // recomecou do quadro 0
}

// audio: dispositivo perdido -> saida nula e o filme continua ate o fim; audio atrasado (fila nunca
// abre espaco) -> descarte contado, sem travar
BS_TEST(video_mw_core_audio_lost_and_late) {
    Fixture f;
    FakeOut* lost = nullptr;
    auto     o    = f.opts();
    o.makeAudio   = [&] { auto p = std::make_unique<FakeOut>(); p->loseAfter = 2; lost = p.get(); return p; };
    {
        tv::MwPlayerCore c(o);
        c.start("E001.SFD", 0);
        int32_t st = 0;
        for (int64_t t = 0; t <= 3000 && st != tv::kMwPlayEnd; t += 16) st = c.status(t * kMs);
        BS_CHECK(st == tv::kMwPlayEnd && c.stats().audioLost && lost);
    }
    o.makeAudio = [] { auto p = std::make_unique<FakeOut>(); p->full = true; return p; };
    tv::MwPlayerCore c(o);
    c.start("E001.SFD", 0);
    int32_t st = 0;
    for (int64_t t = 0; t <= 3000 && st != tv::kMwPlayEnd; t += 16) st = c.status(t * kMs);
    BS_CHECK(st == tv::kMwPlayEnd && c.stats().audioFlushes >= 1);
}

// cor base/offset guardadas (a aplicacao ao quadro e PENDENTE O1b); legenda do mod no quadro
BS_TEST(video_mw_core_colors_and_subtitles) {
    Fixture f;
    writeText(f.mods / "E001.pt.srt", "1\n00:00:00,000 --> 00:00:00,800\nLegenda\n");
    auto o         = f.opts();
    o.subtitleLang = "pt";
    tv::MwPlayerCore c(o);
    c.set_base(0xC0), c.set_offset(0x40);
    BS_CHECK(c.base() == 0xC0 && c.offset() == 0x40);
    c.start("E001.SFD", 0);
    c.status(0), c.status(100 * kMs);
    nv::VideoFrame fr;
    BS_CHECK(c.frame(fr));
    int bright = 0;
    for (size_t i = fr.argb.size() / 2; i < fr.argb.size(); ++i) bright += ((fr.argb[i] >> 8) & 0xFF) > 200;
    BS_CHECK(bright > 50);  // texto branco embaixo (o fundo e vermelho escuro)
}

// manifesto com caminhos fora da pasta: recusados (registrados), o resto do catalogo funciona
BS_TEST(video_catalog_unsafe_paths) {
    TempDir d("caminhos");
    writeText(d.p / "manifest.toml",
              "[[filme]]\nid = 1\noriginal = \"NEWGAME.SFD\"\narquivo = \"..\\\\..\\\\fora.mp4\"\n"
              "[[filme]]\nid = 2\noriginal = \"E017.SFD\"\narquivo = \"C:/Windows/x.mp4\"\n"
              "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"sub/E001.mp4\"\nlegendas = { pt = \"../x.srt\" }\n");
    auto c = nv::MovieCatalog::load({d.p, {}});
    BS_CHECK(!c.find(1) && !c.find(2) && c.find(3) && c.find(3)->subtitles.empty());
    int refused = 0;
    for (const auto& p : c.problems()) refused += p.find("fora da pasta") != std::string::npos;
    BS_CHECK(refused == 3);
}
