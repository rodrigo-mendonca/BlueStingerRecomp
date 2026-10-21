// N5 (D2): saidas de audio atras de IAudioOutput. A nula e testada sempre; o XAudio2 so quando ha um
// dispositivo de saida (maquina sem audio: aviso, sem falha).
#include "test.h"

#include "audio/audio.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
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

// N6-H: estatisticas da saida (revisao Codex 2026-10-21): falta de dados so com a fila vazia depois do primeiro bloco e
// fora de pausa e de descarte; descarte durante a pausa nao religa a voz; reabrir zera tudo.
BS_TEST(audio_output_xaudio2_stats) {
    auto x = na::make_xaudio2_output(200);
    if (!x->open(44100, 2).ok()) {
        std::printf("  (sem dispositivo de audio: XAudio2 nao testado)\n");
        return;
    }
    std::vector<int16_t> s(441 * 2, 0);  // 10 ms
    BS_CHECK(x->write(s).ok() && x->stats().underruns == 0);  // primeiro bloco: nao e falta
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    BS_CHECK(x->write(s).ok() && x->stats().underruns == 1);  // a fila tinha esvaziado
    BS_CHECK(x->write(s).ok() && x->stats().underruns == 1);  // com o bloco anterior na fila: nao e falta
    x->pause(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    x->flush();  // descarte na pausa: a voz nova continua parada
    BS_CHECK(x->write(s).ok());
    const uint64_t p = x->played_frames();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    BS_CHECK(x->played_frames() == p);
    x->pause(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    BS_CHECK(x->write(s).ok() && x->stats().underruns == 2);  // tocou o bloco e esvaziou depois de retomar
    const auto st = x->stats();
    BS_CHECK(st.blocks == 5 && st.writeErrors == 0 && !st.lost && st.maxQueuedFrames >= 441);
    x->close();
    BS_CHECK(!x->write(s).ok() && x->stats().writeErrors == 1);  // fechada: erro de escrita contado
    BS_CHECK(x->open(44100, 2).ok());
    BS_CHECK(x->stats().blocks == 0 && x->stats().underruns == 0 && x->stats().writeErrors == 0 && x->played_frames() == 0);
    BS_CHECK(x->write(s).ok() && x->stats().underruns == 0);  // primeiro bloco da sessao nova
    x->close();
}

// N6-H: seguir a saida padrao (revisao Codex 2026-10-21), com a saida padrao, o relogio e a perda injetados: troca de
// dispositivo reabre (uma vez por segundo no maximo), sem saida padrao espera, a perda (OnCriticalError) reabre sem
// consultar a voz perdida, played_frames nunca regride, as estatisticas ficam e a reabertura na pausa nao toca.
BS_TEST(audio_output_xaudio2_follow_reopen) {
    std::wstring dev  = L"saida-1";
    uint64_t     now  = 0;
    bool         lost = false;
    na::XAudio2Hooks h;
    h.device_id  = [&] { return dev; };
    h.now_ms     = [&] { return now; };
    h.force_lost = [&] { return std::exchange(lost, false); };
    auto x = na::make_xaudio2_output(200, true, h);
    if (!x->open(44100, 2).ok()) {
        std::printf("  (sem dispositivo de audio: XAudio2 nao testado)\n");
        return;
    }
    std::vector<int16_t> s(4410 * 2, 0);  // 100 ms
    BS_CHECK(x->write(s).ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const uint64_t p1 = x->played_frames();
    BS_CHECK(p1 > 2000);
    dev = L"saida-2";
    now = 500;  // antes de 1 s: nao confere ainda
    BS_CHECK(x->write(s).ok() && x->stats().reopens == 0);
    now = 1000;  // trocou a saida padrao: reabre
    BS_CHECK(x->write(s).ok() && x->stats().reopens == 1 && x->stats().blocks == 3);
    BS_CHECK(x->played_frames() >= p1);  // os tocados da voz antiga ficam
    dev = L"";  // nenhuma saida agora: nao reabre, tenta de novo depois
    now = 2000;
    BS_CHECK(x->write(s).ok() && x->stats().reopens == 1);
    dev  = L"saida-2";
    lost = true;  // o dispositivo caiu (como o OnCriticalError)
    now  = 3000;
    const uint64_t p2 = x->played_frames();
    BS_CHECK(x->write(s).ok() && x->stats().reopens == 2 && !x->stats().lost && x->healthy());
    BS_CHECK(x->played_frames() >= p2);
    x->pause(true);
    dev = L"saida-3";
    now = 4000;
    BS_CHECK(x->write(s).ok() && x->stats().reopens == 3);
    const uint64_t p3 = x->played_frames();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    BS_CHECK(x->played_frames() == p3);  // reaberta na pausa: parada
    x->pause(false);
    BS_CHECK(x->stats().writeErrors == 0);
    x->close();
}

// N6-H: no modo do audio do jogo (seguir a saida padrao), uma rajada alem da fila do XAudio2 (o jogo recupera campos
// atrasados) descarta blocos em vez de dar o dispositivo por perdido; antes, o envio falhava e o jogo ficava mudo.
BS_TEST(audio_output_xaudio2_follow_burst) {
    auto x = na::make_xaudio2_output(200, true);
    if (!x->open(44100, 2).ok()) {
        std::printf("  (sem dispositivo de audio: XAudio2 nao testado)\n");
        return;
    }
    std::vector<int16_t> s(176 * 2, 0);  // um tick da musica (4 ms), como o adaptador antigo enviava
    bool ok = true;
    for (int i = 0; i < 200; ++i) ok &= x->write(s).ok();
    const auto st = x->stats();
    BS_CHECK(ok && x->healthy() && st.dropped > 0 && st.writeErrors == 0 && !st.lost);
    BS_CHECK(st.blocks + st.dropped == 200 && st.blocks < 64);
    BS_CHECK(st.cushions == 1);  // a reserva de silencio so com a fila vazia (o inicio)
    BS_CHECK(st.maxQueuedFrames <= 44100 * 200 / 1000);  // o teto do contrato (200 ms), com a reserva e o bloco
    // o modo de sempre (filmes, N5) nao muda: a mesma rajada estoura a fila e conta como erro
    auto y = na::make_xaudio2_output(200);
    if (y->open(44100, 2).ok()) {
        bool all = true;
        for (int i = 0; i < 200; ++i) all &= y->write(s).ok();
        BS_CHECK(!all && y->stats().writeErrors > 0 && y->stats().dropped == 0);
        y->close();
    }
    x->close();
}
