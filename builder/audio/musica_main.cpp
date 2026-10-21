// bs-musica (desenvolvimento, N6 fase F): toca uma musica do disco do usuario pelo sequenciador e pelo sintetizador
// nativos do Manatee (native/audio/manatee_*) e grava um WAV de diagnostico e os eventos com o tempo. Nada sai do disco
// do usuario para o repositorio.
//   bs-musica <BSDATA> <nome (M2A)> <segundos> <saida.wav> [<eventos.tsv>]
#include "audio/aica_dsp.h"
#include "audio/manatee.h"
#include "audio/manatee_seq.h"
#include "audio/manatee_synth.h"
#include "audio/mixer.h"
#include "audio/sd_adapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace nm = bs::native::audio::manatee;
namespace na = bs::native::audio;

namespace {
std::vector<uint8_t> ler(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}
void wav_cabecalho(std::FILE* f, uint32_t taxa, uint64_t quadros) {
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t dados = uint32_t(quadros * 4);
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f), u32(36 + dados), std::fwrite("WAVEfmt ", 1, 8, f), u32(16), u16(1), u16(2), u32(taxa),
        u32(taxa * 4), u16(4), u16(16), std::fwrite("data", 1, 4, f), u32(dados);
}
const char* nome_tipo(nm::MusicEvent::Kind k) {
    switch (k) {
    case nm::MusicEvent::Kind::NoteOn: return "liga";
    case nm::MusicEvent::Kind::NoteOff: return "desliga";
    case nm::MusicEvent::Kind::Control: return "controle";
    case nm::MusicEvent::Kind::Program: return "programa";
    case nm::MusicEvent::Kind::Pitch: return "pitch";
    case nm::MusicEvent::Kind::Pressure: return "pressao";
    case nm::MusicEvent::Kind::End: return "fim";
    }
    return "?";
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "uso: bs-musica <BSDATA> <nome (M2A)> <segundos> <saida.wav> [<eventos.tsv>]\n");
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    const std::string           nome = argv[2];
    const double                segundos = std::atof(argv[3]);
    const auto                  msb = ler(dir / (nome + ".MSB")), mpb = ler(dir / (nome + ".MPB"));
    std::string                 err;
    auto                        seqs = nm::parse_smsb(msb, &err);
    if (!seqs) return std::fprintf(stderr, "%s.MSB: %s\n", nome.c_str(), err.c_str()), 1;
    auto bank = nm::parse_smpb(mpb, &err);
    if (!bank) return std::fprintf(stderr, "%s.MPB: %s\n", nome.c_str(), err.c_str()), 1;
    const auto drvBytes = ler(dir / "MANATEE.DRV");
    auto       drv      = nm::parse_driver_tables(drvBytes, &err);
    if (!drv) return std::fprintf(stderr, "%s\n", err.c_str()), 1;

    constexpr uint32_t kTaxa = 44100;
    na::AudioMixer     mixer(kTaxa, 80);
    // o DSP de efeitos com o programa do disco (BS_SND.MLT), como no jogo; BS_SEM_DSP=1 desliga (comparacao)
    if (!std::getenv("BS_SEM_DSP")) {
        const auto mlt = ler(dir / "BS_SND.MLT");
        if (auto prog = na::parse_aica_dsp_mlt(mlt, &err)) {
            std::array<std::array<float, 2>, 16> gains{};
            for (size_t i = 0; i < 16; ++i)
                if (prog->efsdl[i]) {
                    const float lvl = std::pow(10.0f, -3.0f * float(15 - prog->efsdl[i]) / 20.0f), pan = na::dipan_pan(prog->efpan[i]);
                    gains[i] = {lvl * std::min(1.0f, 1.0f - pan), lvl * std::min(1.0f, 1.0f + pan)};
                }
            mixer.set_dsp(std::make_shared<na::AicaDsp>(*prog), gains);
        } else {
            std::fprintf(stderr, "DSP: %s\n", err.c_str());
        }
    }
    nm::Sequencer      seq((*seqs)[0]);
    nm::Synth          synth(mixer, *bank, *drv);
    synth.set_port_volume(-16);  // o "0" do gerenciador de musica do jogo (8C02AE70 limita a -16)
    std::FILE*         wav = std::fopen(argv[4], "wb");
    std::FILE*         ev  = argc > 5 ? std::fopen(argv[5], "w") : nullptr;
    if (!wav) return std::fprintf(stderr, "nao abre %s\n", argv[4]), 1;
    wav_cabecalho(wav, kTaxa, 0);
    uint64_t chamadaAtual = 0;
    size_t   picoVozes    = 0;
    if (ev) {
        std::fputs("chamada\tsegundos\ttipo\tcanal\ta\tb\tnota\n", ev);
        // as vozes no formato dos key-on do oraculo (n6_oraculo.py --ca): amostra relativa ao banco, tom e TL
        synth.onVoice = [&](const nm::VoiceLog& v) {
            std::fprintf(ev, "%llu\t%.4f\tvoz\t%u\t%u\t%u\t%u\tsa=%06X oct=%d fns=%u tl=%u loop=%d curva=%u/%u cc7=%u tlc=%u pan=%u sdl=%u\n",
                         static_cast<unsigned long long>(chamadaAtual), chamadaAtual / nm::kSequencerHz, v.canal, v.key, v.vel,
                         v.noteId, v.sa, v.pitch.oct, v.pitch.fns, v.tl, int(v.loop), v.curvaIdx, v.curva, v.vol7, v.layerTl,
                         v.dipan, v.sdl);
        };
        // pitch bend: o tom novo de uma voz que segue soando (as linhas "tom" do oraculo)
        synth.onPitch = [&](uint32_t nota, uint8_t canal, const nm::PitchReg& p) {
            std::fprintf(ev, "%llu\t%.4f\ttom\t%u\t0\t0\t%u\toct=%d fns=%u\n", static_cast<unsigned long long>(chamadaAtual),
                         chamadaAtual / nm::kSequencerHz, canal, nota, p.oct, p.fns);
        };
    }

    const uint64_t             chamadas = uint64_t(segundos * nm::kSequencerHz);
    std::vector<nm::MusicEvent> out;
    std::vector<int16_t>       buf;
    uint64_t                   feitos = 0, picos = 0;
    int16_t                    pico   = 0;
    for (uint64_t c = 1; c <= chamadas; ++c) {
        out.clear();
        chamadaAtual = c;
        seq.call(out);
        // depois de cada chamada: o ponteiro de leitura da trilha relativo ao inicio do SMSD e a contagem ([fp+0x10] e
        // [fp+0x24] do driver, o "p" e o "c" das linhas "ticks" do oraculo); o par identifica a chamada. Depois do fim
        // da sequencia nao ha ponteiro valido: nada e escrito (revisao Codex 2026-10-20).
        if (ev && !seq.ended())
            std::fprintf(ev, "%llu\t%.4f\tpos\t0\t0\t0\t0\tp=%u c=%lld\n", static_cast<unsigned long long>(c),
                         c / nm::kSequencerHz, seq.position() - ((*seqs)[0].start - 12), static_cast<long long>(seq.count()));
        for (const auto& e : out) {
            synth.on_event(e);
            if (ev)
                std::fprintf(ev, "%llu\t%.4f\t%s\t%u\t%u\t%u\t%u\n", static_cast<unsigned long long>(e.call), e.call / nm::kSequencerHz,
                             nome_tipo(e.kind), e.canal, e.a, e.b, e.noteId);
        }
        synth.control_tick();
        // a chamada c termina em c * 44100 / 250 = 176,4 amostras cada (o tempo exato do timer do driver)
        const uint64_t ate = c * kTaxa * 2 / 500;
        buf.assign(size_t(ate - feitos) * 2, 0);
        mixer.render(buf);
        for (int16_t s : buf) pico = std::max<int16_t>(pico, int16_t(std::abs(int(s)))), picos += (s == 32767 || s == -32768);
        std::fwrite(buf.data(), 2, buf.size(), wav);
        feitos = ate;
        picoVozes = std::max(picoVozes, synth.sounding());
    }
    wav_cabecalho(wav, kTaxa, feitos);
    std::fclose(wav);
    if (ev) std::fclose(ev);
    const auto& s = synth.stats();
    std::printf("%s: %.1f s, %llu chamadas; notas %llu, vozes %llu, sem camada %llu, recusadas %llu, tom saturado %llu, "
                "falhas de amostra %llu; tempo final %d; acabou %s; pico %d, saturadas %llu\n",
                nome.c_str(), feitos / double(kTaxa), static_cast<unsigned long long>(chamadas),
                static_cast<unsigned long long>(s.notes), static_cast<unsigned long long>(s.voices),
                static_cast<unsigned long long>(s.noLayer), static_cast<unsigned long long>(s.refused),
                static_cast<unsigned long long>(s.saturatedPitch), static_cast<unsigned long long>(s.decodeFailures), seq.tempo(),
                seq.ended() ? "sim" : "nao (laco)", int(pico), static_cast<unsigned long long>(picos));
    std::printf("%s: voltas do laco %llu, primeira volta em %.2f s; pico de vozes simultaneas %zu; reaproveitadas %llu "
                "(com a nota ligada %llu, em soltura a menos de 30 dB %llu); MIXS acima de 20 bits %llu\n",
                nome.c_str(), static_cast<unsigned long long>(seq.loops()), seq.first_loop_call() / nm::kSequencerHz, picoVozes,
                static_cast<unsigned long long>(s.stolen), static_cast<unsigned long long>(s.stolenKeyed),
                static_cast<unsigned long long>(s.stolenLoud), static_cast<unsigned long long>(mixer.dsp_wraps()));
    return 0;
}
