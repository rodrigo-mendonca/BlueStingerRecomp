// N5: casos da revisao do Codex (reviews/codex-20261019-160959.md, na worktree).
#include "test.h"

#include "video/mf_movie.h"
#include "video/movie_build.h"
#include "video/movie_catalog.h"
#include "video/movie_playback.h"
#include "video_mw_core.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace nv = bs::native::video;
namespace na = bs::native::audio;
namespace bv = bs::builder::video;
namespace tv = bs::transitional::video;

namespace {

struct TempDir {
    fs::path p;
    explicit TempDir(const char* tag) {
        p = fs::temp_directory_path() / (std::string("bs-vfix-") + tag + "-" + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(p, ec); }
};

void writeText(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }

void makeMovie(const fs::path& f, int frames) {
    nv::MfMovieWriter w;
    w.open(f, {});
    std::vector<uint32_t> px(320 * 240, 0xFF202020u);
    for (int i = 0; i < frames; ++i) w.write_frame(px);
    std::vector<int16_t> pcm(size_t(44100) * frames / 30 * 2, 2000);
    w.write_audio(pcm);
    w.finish();
}

int64_t audioFrames(const fs::path& f, int64_t skip) {
    nv::MoviePlayback   p;
    nv::PlaybackOptions o;
    o.audioSkipFrames = skip;
    p.open(f, o);
    return int64_t(p.take_audio(100'000'000).size() / 2);
}

constexpr int64_t kMs = 10'000;

struct FailingOut final : na::IAudioOutput {
    bs::native::Result<void> open(uint32_t, uint8_t) override { return {}; }
    size_t                   writable_frames() const override { return 100000; }
    bs::native::Result<void> write(std::span<const int16_t>) override { return bs::native::Error::Io; }
    void                     close() override {}
    void                     pause(bool) override {}
    void                     flush() override {}
    uint64_t                 played_frames() const override { return 0; }
    bool                     healthy() const override { return true; }  // recusa sem se declarar perdida
};

} // namespace

// Builder executado duas vezes na mesma pasta: o manifesto existente e substituido (antes falhava)
BS_TEST(video_build_twice_replaces_manifest) {
    TempDir d("build2");
    fs::create_directories(d.p / "BSDATA");
    bv::MovieBuildOptions o;
    bv::MovieBuildResult  r;
    std::string           err;
    BS_CHECK(bv::build_movies(d.p / "BSDATA", d.p / "video", o, r, &err));
    BS_CHECK(bv::build_movies(d.p / "BSDATA", d.p / "video", o, r, &err) && r.absent.size() == 17);
    BS_CHECK(!fs::exists(d.p / "video" / "manifest.toml.tmp"));
}

// priming do AAC: as amostras pedidas saem do inicio; fim pela fonte original no lugar do EOF do MP4
BS_TEST(video_playback_priming_and_source_end) {
    TempDir        d("priming");
    const fs::path mp4 = d.p / "a.mp4";
    makeMovie(mp4, 30);
    const int64_t all = audioFrames(mp4, 0);
    BS_CHECK(all > 40000 && audioFrames(mp4, 1024) == all - 1024);
    nv::MoviePlayback   p;
    nv::PlaybackOptions o;
    o.end                 = nv::EndRule::LaterOfBoth;
    o.sourceVideoEnd100ns = 500 * kMs;
    o.sourceAudioEnd100ns = 520 * kMs;
    BS_CHECK(p.open(mp4, o));
    p.advance(510 * kMs), p.take_audio(510 * kMs);
    BS_CHECK(!p.finished(510 * kMs));  // video da fonte acabou, audio da fonte ainda nao
    p.advance(520 * kMs);
    BS_CHECK(p.finished(520 * kMs));  // muito antes do EOF do MP4 (1 s)
}

// stop volta ao estado 0 depois de fim e de erro (mwSfdStop grava 0); a saida que recusa o bloco
// segue a politica de dispositivo perdido
BS_TEST(video_mw_core_stop_after_end_and_failing_output) {
    TempDir        d("stop");
    const fs::path data = d.p / "video";
    fs::create_directories(data);
    makeMovie(data / "E001.mp4", 6);
    writeText(data / "manifest.toml", "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\n");
    tv::MwCoreOptions o;
    o.catalog   = {data, {}};
    o.makeAudio = [] { return std::make_unique<FailingOut>(); };
    tv::MwPlayerCore c(o);
    c.start("E001.SFD", 0);
    int32_t st = 0;
    for (int64_t t = 0; t <= 2000 && st != tv::kMwPlayEnd; t += 16) st = c.status(t * kMs);
    BS_CHECK(st == tv::kMwPlayEnd && c.stats().audioLost);
    c.stop();
    BS_CHECK(c.state() == tv::kMwStop && c.status(3000 * kMs) == tv::kMwStop);
    BS_CHECK(c.start("NAO_EXISTE.SFD", 0) == tv::kMwError);
    c.stop();
    BS_CHECK(c.status(0) == tv::kMwStop);
}

// juncao dentro da pasta de mods apontando para fora: recusada pela contencao depois de resolver
BS_TEST(video_catalog_junction_escape) {
    TempDir        d("juncao");
    const fs::path mods = d.p / "mods", fora = d.p / "fora";
    fs::create_directories(mods), fs::create_directories(fora);
    writeText(fora / "x.mp4", "x");
    const std::string cmd = "mklink /J \"" + (mods / "sub").string() + "\" \"" + fora.string() + "\" >nul 2>&1";
    if (std::system(cmd.c_str()) != 0 || !fs::exists(mods / "sub" / "x.mp4")) {
        std::printf("  (sem permissao para criar juncao: caso nao testado)\n");
        return;
    }
    writeText(mods / "manifest.toml", "[[filme]]\nid = 1\noriginal = \"NEWGAME.SFD\"\narquivo = \"sub/x.mp4\"\n");
    auto c = nv::MovieCatalog::load({d.p / "vazio", mods});
    BS_CHECK(!c.find(1));
    bool refused = false;
    for (const auto& p : c.problems()) refused |= p.find("juncao ou link") != std::string::npos;
    BS_CHECK(refused);
    std::error_code ec;
    fs::remove(mods / "sub", ec);  // remove so a juncao, nunca o alvo
}
