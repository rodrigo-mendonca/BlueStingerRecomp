// N5 (D2): saidas de audio atras de IAudioOutput. A nula e testada sempre; o XAudio2 so quando ha um
// dispositivo de saida (maquina sem audio: aviso, sem falha).
#include "test.h"

#include "audio/audio.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace na = bs::native::audio;

BS_TEST(video_audio_null_output) {
    auto n = na::make_null_output();
    BS_CHECK(n->open(44100, 2).ok() && n->healthy());
    std::vector<int16_t> s(882 * 2, 0);  // 20 ms
    BS_CHECK(n->write(s).ok() && n->played_frames() == 882);
    n->pause(true);
    BS_CHECK(n->writable_frames() == 0);
    n->write(s);
    BS_CHECK(n->played_frames() == 882);  // pausada: nao conta
    n->pause(false);
    n->write(s);
    BS_CHECK(n->played_frames() == 1764);
}

// Fila com teto de latencia, tocando, pausa e descarte (flush) no XAudio2.
BS_TEST(video_audio_xaudio2_output) {
    auto x = na::make_xaudio2_output(200);
    if (!x->open(44100, 2).ok()) {
        std::printf("  (sem dispositivo de audio: XAudio2 nao testado)\n");
        return;
    }
    const size_t cap = x->writable_frames();
    BS_CHECK(cap >= 8800 && cap <= 8820);  // 200 ms
    std::vector<int16_t> s(4410 * 2, 0);   // 100 ms de silencio
    BS_CHECK(x->write(s).ok() && x->write(s).ok());
    BS_CHECK(x->writable_frames() < 100);  // fila cheia (teto)
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const uint64_t p1 = x->played_frames();
    BS_CHECK(p1 > 2000 && x->writable_frames() > 2000);  // tocou e liberou espaco
    x->pause(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const uint64_t p2 = x->played_frames();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    BS_CHECK(x->played_frames() == p2);  // pausada nao avanca
    x->pause(false);
    x->flush();
    BS_CHECK(x->writable_frames() >= 8800 && x->healthy());  // fila vazia depois do descarte
    x->close();
}
