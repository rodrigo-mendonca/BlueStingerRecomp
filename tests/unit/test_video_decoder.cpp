// N5: o MoviePlayback (ritmo pelo relogio logico, priming do audio, regra de fim) so depende do
// contrato IMovieDecoder: um decodificador sintetico, sem Media Foundation, toca igual. E a separacao
// que uma implementacao de outra plataforma usa (docs/DECISAO-VULKAN-LINUX.md, secao 3).
#include "test.h"

#include "video/movie_color.h"
#include "video/movie_playback.h"
#include "video/subtitles.h"

#include <memory>
#include <vector>

namespace nv = bs::native::video;

namespace {
// 10 quadros a 10 fps (1 s) e 1 s de audio estereo a 1 kHz, em blocos de 100 amostras
class FakeDecoder final : public nv::IMovieDecoder {
public:
    bool opened = false;
    int  frame = 0, block = 0;
    nv::MovieFormat fmt{4, 2, 10.0, 10'000'000, true, 1000, 2};
    bool open(const std::filesystem::path&, std::string*) override { return opened = true; }
    const nv::MovieFormat& format() const override { return fmt; }
    bool next_frame(nv::VideoFrame& f, std::string*) override {
        if (frame >= 10) return false;
        f.width = 4, f.height = 2, f.time100ns = int64_t(frame) * 1'000'000, f.duration100ns = 1'000'000;
        f.argb.assign(8, 0xFF000000u | uint32_t(frame));
        ++frame;
        return true;
    }
    bool next_audio(std::vector<int16_t>& pcm, int64_t& t, std::string*) override {
        if (block >= 10) return false;
        t = int64_t(block) * 1'000'000;
        pcm.assign(200, int16_t(block));
        ++block;
        return true;
    }
    void close() override {}
};
} // namespace

BS_TEST(video_playback_over_decoder_interface) {
    auto           d   = std::make_unique<FakeDecoder>();
    FakeDecoder*   raw = d.get();
    nv::MoviePlayback p(std::move(d));
    BS_CHECK(p.open("sintetico.mp4") && raw->opened && p.format().width == 4);
    BS_CHECK(p.advance(0) && p.has_frame() && (p.frame().argb[0] & 0xFF) == 0);
    p.advance(4'500'000);  // 0,45 s: quadro 4
    BS_CHECK((p.frame().argb[0] & 0xFF) == 4 && p.frames_skipped() >= 3);
    size_t audio = 0;
    for (int64_t t = 0; t <= 12'000'000; t += 1'000'000) {
        p.advance(t);
        audio += p.take_audio(t).size();
    }
    BS_CHECK(audio == 2000 && p.finished(12'000'000));  // todo o audio entregue; fim pela regra (o ultimo)
}

// a rasterizacao das legendas tambem e uma interface: a fabrica da plataforma a entrega
BS_TEST(video_subtitle_rasterizer_interface) {
    std::unique_ptr<nv::ISubtitleRasterizer> r = nv::make_subtitle_rasterizer();
    BS_CHECK(r != nullptr);
    if (r && r->ok()) {
        std::vector<uint32_t> px(320 * 240, 0xFF000000u);
        r->draw(px, 320, 240, L"Teste");
        int lit = 0;
        for (uint32_t c : px) lit += ((c >> 8) & 0xFF) > 200;
        BS_CHECK(lit > 20);
    }
}

// cor do sprite (video/movie_color.h), portavel: identidade, escurecimento, clareamento, limites, alfa
BS_TEST(video_movie_color_model) {
    nv::MovieColor c;
    c.set(nv::MovieColorModel::identity(), 0xC0, 0x40);  // sem modelo: base e offset nao mudam nada
    BS_CHECK(c.is_identity());
    std::vector<uint32_t> px = {0x80102030u, 0xFFFFFFFFu, 0x00000000u};
    const auto            orig = px;
    c.apply(px);
    BS_CHECK(px == orig);
    c.set({false, true, false}, 0x80, 0);  // escurece: x 128/255
    BS_CHECK(c.map(255) == 128 && c.map(0) == 0 && c.map(100) == 50 && !c.is_identity());
    c.set({false, false, true}, 0xFF, 0x30);  // clareia: + 48, satura em 255
    BS_CHECK(c.map(0) == 48 && c.map(250) == 255);
    c.set({true, false, false}, 0xFF, 0);  // faixa de TV: 0 -> 16, 255 -> 235
    BS_CHECK(c.map(0) == 16 && c.map(255) == 235);
    c.set({true, true, true}, 0xC0, 0);  // o modelo do O1b no NEWGAME: 0,753 x (16 + 0,859 x v)
    BS_CHECK(c.map(0) == 12 && c.map(255) == 177);
    c.set({false, true, true}, 0x00, 0x00);  // limite inferior
    BS_CHECK(c.map(255) == 0);
    std::vector<uint32_t> a = {0x7F80C0FFu};
    c.set({false, true, false}, 0x80, 0);
    c.apply(a);
    BS_CHECK((a[0] >> 24) == 0x7F && ((a[0] >> 16) & 0xFF) == 64 && (a[0] & 0xFF) == 128);  // alfa preservado
}
