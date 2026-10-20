// N5: arquitetura de mods de video (arquivos e metadados sinteticos pequenos).
#include "test.h"

#include "video/mf_movie.h"
#include "video/movie_catalog.h"
#include "video/subtitles.h"
#include "video_mw_core.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace nv = bs::native::video;
namespace tv = bs::transitional::video;

namespace {
constexpr int64_t kMs = 10'000;
struct Dir {
    fs::path p = fs::temp_directory_path() / ("bs-vmods-" + std::to_string(std::random_device{}()));
    fs::path data = p / "data" / "video", mods = p / "mods" / "video";
    Dir() { fs::create_directories(data), fs::create_directories(mods); }
    ~Dir() { std::error_code ec; fs::remove_all(p, ec); }
};
void movie(const fs::path& f, uint32_t w, uint32_t h, uint32_t fps, int frames) {
    nv::MfMovieWriter mw;
    nv::WriterOptions o;
    o.width = w, o.height = h, o.fpsNum = fps, o.audio = false;
    mw.open(f, o);
    std::vector<uint32_t> px(size_t(w) * h, 0xFF404040u);
    for (int i = 0; i < frames; ++i) mw.write_frame(px);
    mw.finish();
}
int32_t play(tv::MwPlayerCore& c, const char* name) {
    c.start(name, 0);
    int32_t st = c.status(0);
    for (int64_t t = 0; t <= 5000 && st == tv::kMwPlaying; t += 16) st = c.status(t * kMs);
    return st;
}
} // namespace

// sem mod, o convertido do Builder (fallback); com mod por nome, o mod (outra resolucao e fps); o
// original convertido continua no disco e volta se o mod sair
BS_TEST(video_mods_replace_and_fallback) {
    Dir d;
    movie(d.data / "E001.mp4", 320, 240, 30, 9);
    std::ofstream(d.data / "manifest.toml") << "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\n";
    tv::MwCoreOptions o;
    o.catalog = {d.data, d.mods};
    {
        tv::MwPlayerCore c(o);
        BS_CHECK(play(c, "E001.SFD") == tv::kMwPlayEnd && !c.entry()->mod);
        nv::VideoFrame f;
    }
    movie(d.mods / "E001.mp4", 1920, 1080, 60, 30);  // remaster 1080p60, 0,5 s
    {
        tv::MwPlayerCore c(o);
        c.start("E001.SFD", 0);
        c.status(0);
        nv::VideoFrame f;
        BS_CHECK(c.entry()->mod && c.frame(f) && f.width == 1920 && f.height == 1080);
        int32_t st = tv::kMwPlaying;
        for (int64_t t = 0; t <= 5000 && st == tv::kMwPlaying; t += 16) st = c.status(t * kMs);
        BS_CHECK(st == tv::kMwPlayEnd && c.stats().framesShown >= 25);  // 30 quadros a 60 fps, consultas a cada 16 ms
    }
    fs::remove(d.mods / "E001.mp4");
    tv::MwPlayerCore c(o);
    BS_CHECK(play(c, "E001.SFD") == tv::kMwPlayEnd && !c.entry()->mod && fs::exists(d.data / "E001.mp4"));
}

// legendas: idioma exato, idioma base como fallback, nunca outro idioma
BS_TEST(video_mods_subtitle_language_fallback) {
    Dir d;
    movie(d.data / "E001.mp4", 320, 240, 30, 9);
    std::ofstream(d.data / "manifest.toml") << "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\n";
    std::ofstream(d.mods / "E001.pt.srt") << "1\n00:00:00,000 --> 00:00:01,000\nOla\n";
    std::ofstream(d.mods / "E001.en.vtt") << "WEBVTT\n\n00:00.000 --> 00:01.000\nHello\n";
    auto bottomWhite = [&](const char* lang) {
        tv::MwCoreOptions o;
        o.catalog      = {d.data, d.mods};
        o.subtitleLang = lang;
        tv::MwPlayerCore c(o);
        c.start("E001.SFD", 0);
        c.status(0), c.status(100 * kMs);
        nv::VideoFrame f;
        c.frame(f);
        int n = 0;
        for (size_t i = f.argb.size() / 2; i < f.argb.size(); ++i) n += ((f.argb[i] >> 8) & 0xFF) > 200;
        return n;
    };
    BS_CHECK(bottomWhite("pt") > 30 && bottomWhite("PT-BR") > 30 && bottomWhite("en") > 30);
    BS_CHECK(bottomWhite("es") == 0 && bottomWhite("") == 0);  // sem espanhol: sem legenda (nao cai em outro)
    // chave do manifesto em caixa mista ("ES") com pedido regional ("es-MX")
    std::ofstream(d.mods / "fala.srt") << "1\n00:00:00,000 --> 00:00:01,000\nHola\n";
    std::ofstream(d.mods / "manifest.toml") << "[[filme]]\nid = 3\narquivo = \"E001b.mp4\"\nlegendas = { ES = \"fala.srt\" }\n";
    movie(d.mods / "E001b.mp4", 320, 240, 30, 9);
    BS_CHECK(bottomWhite("es-MX") > 30);
}

// cor do sprite (O1b): filme do Builder na faixa de TV e modulado pela cor base; offset aditivo; um mod
// fica na faixa cheia (so a cor base e o offset do jogo)
BS_TEST(video_color_base_offset_and_range) {
    Dir d;
    movie(d.data / "E001.mp4", 320, 240, 30, 9);  // cinza 0x40 = 64
    std::ofstream(d.data / "manifest.toml") << "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\n";
    auto channel = [&](uint32_t base, uint32_t off) {
        tv::MwCoreOptions o;
        o.catalog = {d.data, d.mods};
        o.color   = {true, true, true};  // o modelo medido no O1b (o padrao e a identidade)
        tv::MwPlayerCore c(o);
        c.set_base(base), c.set_offset(off);
        c.start("E001.SFD", 0);
        c.status(0), c.status(100 * kMs);
        nv::VideoFrame f;
        c.frame(f);
        return int((f.argb[f.argb.size() / 2] >> 8) & 0xFF);
    };
    // 0xC0/255 x (16 + 64 x 219/255) = 53,4
    BS_CHECK(std::abs(channel(0xC0, 0) - 53) <= 2);
    BS_CHECK(std::abs(channel(0xFF, 0) - 71) <= 2);           // so a faixa de TV
    BS_CHECK(std::abs(channel(0xC0, 0x20) - (53 + 32)) <= 2);  // offset somado
    movie(d.mods / "E001.mp4", 320, 240, 30, 9);              // mod: faixa cheia
    BS_CHECK(std::abs(channel(0xC0, 0) - 48) <= 2);           // 64 x 0,753
    BS_CHECK(std::abs(channel(0xFF, 0) - 64) <= 2);           // identidade
}

// sem modelo (o padrao ate o O1b confirmar): o quadro sai como o arquivo, qualquer que seja a cor do jogo
BS_TEST(video_color_default_is_identity) {
    Dir d;
    movie(d.data / "E001.mp4", 320, 240, 30, 9);
    std::ofstream(d.data / "manifest.toml") << "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\n";
    tv::MwCoreOptions o;
    o.catalog = {d.data, d.mods};
    tv::MwPlayerCore c(o);
    c.set_base(0xC0), c.set_offset(0x20);
    c.start("E001.SFD", 0);
    c.status(0), c.status(100 * kMs);
    nv::VideoFrame f;
    c.frame(f);
    BS_CHECK(std::abs(int((f.argb[f.argb.size() / 2] >> 8) & 0xFF) - 64) <= 2);
}

// mod incompativel (o Media Foundation nao decodifica): estado de erro com diagnostico; o jogo segue
BS_TEST(video_mods_incompatible_file) {
    Dir d;
    std::ofstream(d.data / "manifest.toml") << "[[filme]]\nid = 1\noriginal = \"NEWGAME.SFD\"\nausente = true\n";
    std::ofstream(d.mods / "NEWGAME.mp4") << std::string(8192, 'z');
    std::string       log;
    tv::MwCoreOptions o;
    o.catalog = {d.data, d.mods};
    o.log     = [&](const std::string& m) { log += m + "\n"; };
    tv::MwPlayerCore c(o);
    BS_CHECK(c.start("NEWGAME.SFD", 0) == tv::kMwError && c.status(0) == tv::kMwError);
    BS_CHECK(log.find("NEWGAME.SFD") != std::string::npos);
}

// Legendas EN (docs/LEGENDAS-EN-PLANO.md): as copias no leiaute do N5 (mods\video\<NOME>.en.srt) que o pacote gerado
// traz em video\ sao achadas pelo catalogo de filmes e lidas pelo parser do N5 (que as desenha hoje). Com BS_LEGENDAS
// (pasta de trabalho do pipeline) e BS_GAME_DATA (o disco; data\video ao lado).
BS_TEST(video_legendas_en_geradas) {
    const char* trab = std::getenv("BS_LEGENDAS");
    const char* gd   = std::getenv("BS_GAME_DATA");
    if (!trab || !gd) return;
    const auto cat = bs::native::video::MovieCatalog::load(
        {std::filesystem::path(gd).parent_path() / "video", std::filesystem::path(trab) / "mod" / "legendas-en" / "video"});
    const auto* ng = cat.find("NEWGAME.SFD");
    BS_CHECK(ng && ng->subtitles.count("en") == 1);
    if (!ng || !ng->subtitles.count("en")) return;
    std::vector<bs::native::video::SubtitleCue> cues;
    std::string                                 err;
    BS_CHECK(bs::native::video::load_subtitles(ng->subtitles.at("en"), cues, &err) && cues.size() >= 10);
    for (size_t i = 1; i < cues.size(); ++i) BS_CHECK(cues[i - 1].end100ns <= cues[i].start100ns);
}
