// N5: filmes nativos (docs/N5-PLANO.md). So amostras sinteticas: nenhum conteudo do jogo.
//   - demux de um MPEG-1 Program Stream montado aqui (pack, sistema, PES com PTS, padding, fim);
//   - ADX: bloco montado a mao contra a formula, ida e volta de um seno, cabecalhos invalidos;
//   - catalogo: manifesto do Builder, mod por nome e por manifesto, legendas, problemas;
//   - Media Foundation: MP4 sintetico (cores por quadro e tom) gravado e lido de volta.
#include "test.h"

#include "video/mf_movie.h"
#include "video/movie_catalog.h"
#include "video/movie_playback.h"
#include "video/movie_build.h"
#include "video/sfd_media.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace bv = bs::builder::video;
namespace nv = bs::native::video;

namespace {

struct TempDir {
    fs::path p;
    explicit TempDir(const char* tag) {
        p = fs::temp_directory_path() / (std::string("bs-video-") + tag + "-" + std::to_string(std::random_device{}()));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(p, ec); }
};

void writeText(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }

// PES MPEG-1 com PTS
void pes(std::vector<uint8_t>& o, uint8_t id, const std::vector<uint8_t>& payload, int64_t pts) {
    std::vector<uint8_t> h = {0xFF, 0xFF};  // stuffing
    if (pts >= 0) {
        h.push_back(uint8_t(0x21 | ((pts >> 29) & 0x0E)));
        h.push_back(uint8_t(pts >> 22)), h.push_back(uint8_t(((pts >> 14) & 0xFE) | 1));
        h.push_back(uint8_t(pts >> 7)), h.push_back(uint8_t(((pts << 1) & 0xFE) | 1));
    } else h.push_back(0x0F);
    const size_t len = h.size() + payload.size();
    o.insert(o.end(), {0, 0, 1, id, uint8_t(len >> 8), uint8_t(len)});
    o.insert(o.end(), h.begin(), h.end());
    o.insert(o.end(), payload.begin(), payload.end());
}
void pack(std::vector<uint8_t>& o) { o.insert(o.end(), {0, 0, 1, 0xBA, 0x21, 0, 1, 0, 1, 0x80, 0, 1}); }

} // namespace

BS_TEST(video_sfd_demux_synthetic_ps) {
    std::vector<uint8_t> f(32, 0);
    std::memcpy(f.data(), "Sofdec Stream   ", 16);  // cabecalho proprio antes do 1o pack
    pack(f);
    f.insert(f.end(), {0, 0, 1, 0xBB, 0, 6, 1, 2, 3, 4, 5, 6});  // cabecalho de sistema
    pes(f, 0xE0, {0, 0, 1, 0xB3, 0x14, 0x00, 0xF0, 0x15}, 90000);
    pes(f, 0xC0, {0x80, 0x00, 0x01}, 90000);
    pack(f);
    pes(f, 0xBE, {0xFF, 0xFF, 0xFF}, -1);  // padding: ignorado
    pes(f, 0xE0, {9, 9}, -1);
    pes(f, 0xC0, {7}, 93003);
    f.insert(f.end(), {0, 0, 1, 0xB9});
    f.insert(f.end(), 64, 0);  // preenchimento do setor
    bv::SfdStreams s;
    std::string    err;
    BS_CHECK(bv::demux_sfd(f, s, &err));
    BS_CHECK((s.video == std::vector<uint8_t>{0, 0, 1, 0xB3, 0x14, 0x00, 0xF0, 0x15, 9, 9}));
    BS_CHECK((s.audio == std::vector<uint8_t>{0x80, 0x00, 0x01, 7}));
    BS_CHECK(s.packs == 2 && s.videoPes == 2 && s.audioPes == 2 && s.firstVideoPts == 90000 && s.firstAudioPts == 90000);
    // truncado e sem pack
    std::vector<uint8_t> bad(f.begin(), f.begin() + 60);
    BS_CHECK(!bv::demux_sfd(bad, s, &err) && !err.empty());
    BS_CHECK(!bv::demux_sfd(std::vector<uint8_t>(100, 0x55), s, &err));
}

// Bloco montado a mao: escala, nibbles com sinal (alto primeiro) e o preditor de 2 coeficientes,
// conferidos contra a formula escrita aqui (independente do decodificador).
BS_TEST(video_adx_block_by_hand) {
    std::vector<int16_t> sil(64, 0);
    auto                 adx = bv::adx_encode(sil, 1, 44100);  // cabecalho valido; os blocos sao trocados
    bv::AdxInfo          h;
    BS_CHECK(bv::adx_parse_header(adx, h));
    BS_CHECK(h.channels == 1 && h.sampleRate == 44100 && h.totalSamples == 64 && h.highpass == 500 && h.version == 3);
    int32_t c1, c2;
    bv::adx_coefficients(500, 44100, c1, c2);
    BS_CHECK(c1 == h.coef1 && c2 == h.coef2 && c1 > 7000 && c1 < 8192 && c2 < -3000 && c2 > -4096);
    const size_t d = h.dataOffset;
    uint8_t      blk[18] = {0x01, 0x00};  // escala 256
    for (int i = 0; i < 16; ++i) blk[2 + i] = uint8_t(((i & 7) << 4) | ((8 + (i & 7)) & 0xF));  // +n e -(8-n)
    std::memcpy(adx.data() + d, blk, 18);
    std::memcpy(adx.data() + d + 18, blk, 18);
    std::vector<int16_t> pcm;
    BS_CHECK(bv::adx_decode(adx, pcm));
    int32_t h0 = 0, h1 = 0;
    bool    same = pcm.size() == 64;
    for (int k = 0; k < 64 && same; ++k) {
        const uint8_t b  = blk[2 + (k % 32) / 2];
        const int     nb = (k & 1) ? (b & 0xF) : (b >> 4);
        const int32_t n  = nb >= 8 ? nb - 16 : nb;
        int32_t       s  = n * 256 + ((c1 * h0 + c2 * h1) >> 12);
        s                = std::clamp(s, -32768, 32767);
        same &= pcm[size_t(k)] == s;
        h1 = h0, h0 = s;
    }
    BS_CHECK(same);
}

BS_TEST(video_adx_roundtrip_and_errors) {
    // seno estereo de 1 s (440 Hz e 660 Hz)
    const uint32_t       rate = 44100;
    std::vector<int16_t> pcm(rate * 2);
    for (uint32_t i = 0; i < rate; ++i) {
        pcm[2 * i]     = int16_t(12000 * std::sin(2 * std::numbers::pi * 440 * i / rate));
        pcm[2 * i + 1] = int16_t(9000 * std::sin(2 * std::numbers::pi * 660 * i / rate));
    }
    const auto           adx = bv::adx_encode(pcm, 2, rate);
    std::vector<int16_t> back;
    bv::AdxInfo          h;
    std::string          err;
    BS_CHECK(bv::adx_decode(adx, back, &h, &err) && back.size() == pcm.size());
    double sig = 0, noise = 0;
    for (size_t i = 0; i < pcm.size(); ++i) sig += double(pcm[i]) * pcm[i], noise += double(pcm[i] - back[i]) * (pcm[i] - back[i]);
    BS_CHECK(10 * std::log10(sig / std::max(noise, 1.0)) > 20.0);  // SNR do ADX de 4 bits
    auto bad = adx;
    bad[0] = 0;
    BS_CHECK(!bv::adx_decode(bad, back, nullptr, &err) && err.find("0x8000") != std::string::npos);
    bad = adx, bad[4] = 2;
    BS_CHECK(!bv::adx_decode(bad, back, nullptr, &err));
    bad = adx, bad[19] = 8;  // cifrado
    BS_CHECK(!bv::adx_decode(bad, back, nullptr, &err));
    bad.assign(adx.begin(), adx.begin() + ptrdiff_t(adx.size() / 2));  // truncado
    BS_CHECK(!bv::adx_decode(bad, back, nullptr, &err) && err.find("truncado") != std::string::npos);
}

BS_TEST(video_catalog_builder_and_mods) {
    TempDir d("catalogo");
    const fs::path data = d.p / "data" / "video", mods = d.p / "mods" / "video";
    fs::create_directories(data), fs::create_directories(mods);
    writeText(data / "NEWGAME.mp4", "x"), writeText(data / "E001.mp4", "x");
    writeText(data / "manifest.toml",
              "versao = 1\n"
              "[[filme]]\nid = 1\noriginal = \"NEWGAME.SFD\"\narquivo = \"NEWGAME.mp4\"\nquadros = 300\nfps = 30.0\nlargura = 320\naltura = 240\nsha256 = \"ab\"\n"
              "[[filme]]\nid = 3\noriginal = \"E001.SFD\"\narquivo = \"E001.mp4\"\nquadros = 90\nfps = 30.0\n"
              "[[filme]]\nid = 0\noriginal = \"OPENING.SFD\"\narquivo = \"OPENING.mp4\"\n"
              "[[filme]]\noriginal = \"SEM_ID.SFD\"\n");
    {
        auto c = nv::MovieCatalog::load({data, {}});
        const auto* e = c.find(1);
        BS_CHECK(e && !e->mod && e->frames == 300 && e->width == 320 && e->fps == 30.0 && e->file == data / "NEWGAME.mp4");
        BS_CHECK(c.find("newgame.sfd") == e && c.find(99) == nullptr);
        BS_CHECK(c.problems().size() == 2);  // OPENING sem arquivo e entrada sem id
    }
    // mod por nome (4K, outra duracao) + legendas; mod por manifesto para o E001
    writeText(mods / "NEWGAME.MP4", "y");
    writeText(mods / "NEWGAME.pt.srt", "1\n00:00:01,000 --> 00:00:02,000\nOla\n");
    writeText(mods / "NEWGAME.en.vtt", "WEBVTT\n");
    writeText(mods / "e001_remaster.mp4", "z");
    writeText(mods / "manifest.toml", "[[filme]]\nid = 3\narquivo = \"e001_remaster.mp4\"\nlargura = 1920\naltura = 1440\nlegendas = { es = \"e001.es.srt\" }\n");
    auto        c  = nv::MovieCatalog::load({data, mods});
    const auto* ng = c.find(1);
    BS_CHECK(ng && ng->mod && ng->file == mods / "NEWGAME.MP4" && ng->frames == 0 && ng->sha256.empty());
    BS_CHECK(ng->subtitles.size() == 2 && ng->subtitles.at("pt") == mods / "NEWGAME.pt.srt" && ng->subtitles.count("en"));
    const auto* e1 = c.find(3);
    BS_CHECK(e1 && e1->mod && e1->file == mods / "e001_remaster.mp4" && e1->width == 1920 && e1->original == "E001.SFD");
    BS_CHECK(e1->subtitles.count("es") == 1);
    // manifesto quebrado: problema registrado, catalogo do Builder intacto
    writeText(mods / "manifest.toml", "[[filme]\nid = ");
    auto c2 = nv::MovieCatalog::load({data, mods});
    BS_CHECK(c2.find(3) && !c2.find(3)->mod && !c2.problems().empty());
}

// MP4 sintetico pelo Media Foundation: 30 quadros de cor solida diferentes (320x240, 30 fps) e 1 s
// de tom estereo; lido de volta, os quadros saem em ordem, com os tempos de 1/30 s e as cores
// proximas (H.264 com perdas); o audio dura ~1 s e nao e silencio.
BS_TEST(video_mf_roundtrip_synthetic) {
    TempDir                 d("mf");
    const fs::path          mp4 = d.p / "teste.mp4";
    nv::MfMovieWriter       w;
    std::string             err;
    nv::WriterOptions       o;
    if (!w.open(mp4, o, &err)) {
        std::printf("  (sem Media Foundation/H.264 nesta maquina: %s)\n", err.c_str());
        BS_CHECK(false);
        return;
    }
    auto color = [](int i) { return 0xFF000000u | uint32_t((i * 8) & 0xFF) << 16 | uint32_t((255 - i * 8) & 0xFF) << 8 | 0x40u; };
    std::vector<uint32_t> frame(320 * 240);
    for (int i = 0; i < 30; ++i) {
        std::fill(frame.begin(), frame.end(), color(i));
        BS_CHECK(w.write_frame(frame, &err));
    }
    std::vector<int16_t> pcm(44100 * 2);
    for (size_t i = 0; i < 44100; ++i) pcm[2 * i] = pcm[2 * i + 1] = int16_t(8000 * std::sin(2 * std::numbers::pi * 440 * double(i) / 44100));
    BS_CHECK(w.write_audio(pcm, &err));
    BS_CHECK(w.finish(&err));

    nv::MfMovieReader r;
    BS_CHECK(r.open(mp4, &err));
    const auto& f = r.format();
    BS_CHECK(f.width == 320 && f.height == 240 && std::abs(f.fps - 30.0) < 0.01 && f.hasAudio && f.sampleRate == 44100 && f.channels == 2);
    BS_CHECK(f.duration100ns > 9'000'000 && f.duration100ns < 11'500'000);
    nv::VideoFrame vf;
    int            n = 0;
    bool           order = true, colors = true;
    while (r.next_frame(vf, &err)) {
        order &= std::llabs(vf.time100ns - int64_t(n) * 10'000'000 / 30) < 20'000;
        const uint32_t px = vf.argb[size_t(120) * 320 + 160], want = color(n);
        for (int sh : {16, 8, 0}) colors &= std::abs(int((px >> sh) & 0xFF) - int((want >> sh) & 0xFF)) <= 12;
        ++n;
    }
    BS_CHECK(n == 30 && order && colors);
    std::vector<int16_t> a;
    int64_t              t = 0;
    size_t               samples = 0;
    double               energy = 0;
    while (r.next_audio(a, t, &err)) {
        samples += a.size() / 2;
        for (int16_t s : a) energy += double(s) * s;
    }
    BS_CHECK(samples > 40000 && samples < 50000 && energy > 1e9);
}

BS_TEST(video_mf_errors) {
    TempDir           d("mf_erros");
    nv::MfMovieReader r;
    std::string       err;
    BS_CHECK(!r.open(d.p / "nao_existe.mp4", &err) && err.find("nao encontrado") != std::string::npos);
    writeText(d.p / "lixo.mp4", std::string(4096, 'x'));
    BS_CHECK(!r.open(d.p / "lixo.mp4", &err) && !err.empty());
    nv::MfMovieWriter w;
    nv::WriterOptions o;
    o.width = 321;  // impar: H.264 recusa
    BS_CHECK(!w.open(d.p / "x.mp4", o, &err));
}

// Reproducao pelo relogio logico (MP4 sintetico de 30 quadros, 1 s): o quadro mostrado e o ultimo
// com tempo <= posicao; relogio parado repete; salto pula quadros (contados); o audio sai ate a
// posicao + antecedencia; o fim segue a regra (video em 1,0 s; o AAC pode passar um pouco).
BS_TEST(video_playback_by_logical_clock) {
    TempDir           d("playback");
    const fs::path    mp4 = d.p / "p.mp4";
    nv::MfMovieWriter w;
    std::string       err;
    BS_CHECK(w.open(mp4, {}, &err));
    std::vector<uint32_t> fr(320 * 240);
    for (int i = 0; i < 30; ++i) {
        std::fill(fr.begin(), fr.end(), 0xFF000000u | uint32_t(i * 8) << 16);
        w.write_frame(fr, &err);
    }
    std::vector<int16_t> pcm(44100 * 2, 1000);
    w.write_audio(pcm, &err);
    BS_CHECK(w.finish(&err));
    auto red = [](const nv::VideoFrame& f) { return int((f.argb[size_t(120) * 320 + 160] >> 16) & 0xFF); };

    nv::MoviePlayback p;
    nv::PlaybackOptions o;
    o.end = nv::EndRule::VideoEnd;
    BS_CHECK(p.open(mp4, o, &err));
    const int64_t ms = 10'000;
    BS_CHECK(p.advance(0) && p.has_frame() && std::abs(red(p.frame()) - 0) <= 12);
    BS_CHECK(!p.advance(10 * ms));                                  // ainda o quadro 0 (1/30 s = 33,3 ms)
    BS_CHECK(p.advance(34 * ms) && std::abs(red(p.frame()) - 8) <= 12);  // quadro 1
    BS_CHECK(!p.advance(34 * ms));                                  // relogio parado: repete
    BS_CHECK(p.advance(500 * ms) && std::abs(red(p.frame()) - 15 * 8) <= 12 && p.frames_skipped() == 13);
    auto a1 = p.take_audio(500 * ms);
    BS_CHECK(a1.size() >= size_t(44100 * 2 * 0.65) && a1.size() <= size_t(44100 * 2 * 0.80));  // ~0,7 s (0,5 + 0,2)
    BS_CHECK(!p.finished(990 * ms));
    p.advance(990 * ms);
    BS_CHECK(!p.finished(990 * ms));
    p.advance(1'100 * ms);
    p.take_audio(1'100 * ms);
    BS_CHECK(p.finished(1'100 * ms) && std::llabs(p.video_end100ns() - 10'000'000) < 20'000 && p.frames_shown() == 4);  // 0, 34 ms, 500 ms e 990 ms (quadro 29)
    // regra pelo audio e pelo ultimo: precisam do audio consumido
    nv::MoviePlayback q;
    o.end = nv::EndRule::LaterOfBoth;
    BS_CHECK(q.open(mp4, o, &err));
    q.advance(1'100 * ms);
    BS_CHECK(!q.finished(1'100 * ms));
    q.take_audio(3'000 * ms);
    q.advance(3'000 * ms);
    BS_CHECK(q.finished(3'000 * ms) && q.audio_end100ns() >= 10'000'000);
}

// Builder sem nenhum SFD: os 17 IDs vao para o manifesto como ausentes; o catalogo os le assim e um mod
// pelo nome do original os fornece (inclusive o OPENING, que o disco US nao tem).
BS_TEST(video_build_absent_and_mod_supplies) {
    TempDir                 d("build");
    const fs::path          bsdata = d.p / "BSDATA", out = d.p / "data" / "video", mods = d.p / "mods" / "video";
    fs::create_directories(bsdata), fs::create_directories(mods);
    bv::MovieBuildOptions   o;
    bv::MovieBuildResult    r;
    std::string             err;
    BS_CHECK(bv::build_movies(bsdata, out, o, r, &err) && r.absent.size() == 17 && r.converted == 0);
    auto c = nv::MovieCatalog::load({out, mods});
    BS_CHECK(c.size() == 17 && c.find(0) && c.find(0)->absent && c.find(0)->original == "OPENING.SFD" && c.problems().empty());
    writeText(mods / "OPENING.mp4", "x");
    auto c2 = nv::MovieCatalog::load({out, mods});
    BS_CHECK(c2.find(0) && !c2.find(0)->absent && c2.find(0)->mod && c2.find(0)->file == mods / "OPENING.mp4");
    BS_CHECK(c2.find(1) && c2.find(1)->absent);  // NEWGAME: sem fonte e sem mod
}
