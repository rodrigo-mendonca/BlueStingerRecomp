// N5 (D3): legendas externas (SRT e WebVTT) e o desenho por DirectWrite sobre o quadro.
#include "test.h"

#include "video/subtitles.h"

#include <string>
#include <vector>

namespace nv = bs::native::video;

BS_TEST(video_subtitles_srt_and_vtt) {
    const std::string srt =
        "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,500\r\n<i>Ola,</i> Eliot!\r\nSegunda linha\r\n\r\n"
        "2\r\n00:00:02,000 --> 00:00:03,000\r\n{\\an8}Sobreposta\r\n\r\n"
        "3\r\n00:00:05,000 --> 00:00:04,000\r\ninvertida\r\n";
    std::vector<nv::SubtitleCue> c;
    std::vector<std::string>     warn;
    BS_CHECK(nv::parse_subtitles(srt, c, &warn) && c.size() == 2 && warn.size() == 1);
    BS_CHECK(c[0].start100ns == 10'000'000 && c[0].end100ns == 25'000'000 && c[0].text == L"Ola, Eliot!\nSegunda linha");
    BS_CHECK(nv::subtitle_at(c, 5'000'000).empty());
    BS_CHECK(nv::subtitle_at(c, 21'000'000) == L"Ola, Eliot!\nSegunda linha\nSobreposta");
    BS_CHECK(nv::subtitle_at(c, 26'000'000) == L"Sobreposta" && nv::subtitle_at(c, 30'000'000).empty());

    const std::string vtt =
        "WEBVTT\n\nNOTE comentario\n\n00:01.000 --> 00:02.000 line:90% align:center\n<c.fala>Dinossauros</c> \xC3\xA0 frente\n\n"
        "intro\n01:00:00.000 --> 01:00:01.500\nUma hora\n";
    BS_CHECK(nv::parse_subtitles(vtt, c) && c.size() == 2);
    BS_CHECK(c[0].start100ns == 10'000'000 && c[0].text == L"Dinossauros à frente");
    BS_CHECK(c[1].start100ns == 36'000'000'000LL && c[1].end100ns == 36'015'000'000LL);
}

BS_TEST(video_subtitles_draw) {
    nv::SubtitleRenderer r;
    BS_CHECK(r.ok());
    const uint32_t        w = 320, h = 240;
    std::vector<uint32_t> f(size_t(w) * h, 0xFF101010u);
    r.draw(f, w, h, L"Legenda de teste");
    int bright = 0, topChanged = 0;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t p = f[size_t(y) * w + x];
            if (y < h / 2 && p != 0xFF101010u) ++topChanged;
            if (y >= h / 2 && ((p >> 16) & 0xFF) > 200 && ((p >> 8) & 0xFF) > 200 && (p & 0xFF) > 200) ++bright;
            BS_CHECK((p >> 24) == 0xFF);
        }
    BS_CHECK(bright > 100 && topChanged == 0);  // texto branco embaixo, a metade de cima intacta
    // texto vazio nao mexe no quadro; mod 4K: proporcional, ainda embaixo
    std::vector<uint32_t> g(size_t(w) * h, 0xFF101010u);
    r.draw(g, w, h, L"");
    BS_CHECK(g == std::vector<uint32_t>(size_t(w) * h, 0xFF101010u));
    std::vector<uint32_t> big(size_t(3840) * 2160, 0xFF000000u);
    r.draw(big, 3840, 2160, L"4K");
    int bigBright = 0;
    for (size_t i = size_t(3840) * 1080; i < big.size(); ++i) bigBright += ((big[i] >> 16) & 0xFF) > 200;
    BS_CHECK(bigBright > 5000);
}
