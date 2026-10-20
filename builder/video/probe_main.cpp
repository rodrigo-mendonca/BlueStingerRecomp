// N5 (desenvolvimento): confere os filmes do disco sem converter. Para cada .SFD: demux, quadros
// MPEG-1 (codigos de inicio de imagem), duracao do video (30 fps declarados no cabecalho de
// sequencia) e do audio ADX decodificado, e o pico do PCM (saturacao indica decodificacao errada).
//   bs-video-probe <pasta BSDATA ou arquivo .SFD> [...]
//   bs-video-probe --converter <pasta de saida> <pasta BSDATA ou .SFD> [...]: converte em MP4 e mede a
//   fidelidade (PSNR/SSIM por quadro, atraso e SNR do AAC)
#include "video/movie_build.h"
#include "video/sfd_convert.h"
#include "video/sfd_media.h"

#include <cstdio>
extern "C" {
#include "pl_mpeg.h"
plm_frame_t* bs_plm_video_flush(plm_video_t* self);

}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
namespace bv = bs::builder::video;

static fs::path g_out;

// BS_PROBE_RAW=<arquivo>: quadros do pl_mpeg em RGB24 (comparacao com outra decodificacao)
static void dumpRaw(const bv::SfdStreams& s, const char* path) {
    std::vector<uint8_t> es = s.video;
    plm_buffer_t*        b  = plm_buffer_create_with_memory(es.data(), es.size(), 0);
    plm_buffer_signal_end(b);
    plm_video_t*         v = plm_video_create_with_buffer(b, 1);
    std::ofstream        o(path, std::ios::binary);
    std::vector<uint8_t> rgb;
    int                  n = 0;
    auto next = [&] { plm_frame_t* x = plm_video_decode(v); return x ? x : bs_plm_video_flush(v); };
    for (plm_frame_t* f = next(); f; f = next(), ++n) {
        rgb.resize(size_t(f->width) * f->height * 3);
        plm_frame_to_rgb(f, rgb.data(), int(f->width) * 3);
        o.write(reinterpret_cast<const char*>(rgb.data()), std::streamsize(rgb.size()));
    }
    plm_video_destroy(v);
    std::printf("pl_mpeg: %d quadros -> %s\n", n, path);
}

static int convert(const fs::path& p) {
    const fs::path      mp4 = g_out / (p.stem().string() + ".mp4");
    bv::ConvertResult   r;
    bv::Fidelity        f;
    std::string         err;
    bv::ConvertOptions co;
    if (const char* b = std::getenv("BS_PROBE_BITRATE")) co.videoBitrate = uint32_t(std::strtoul(b, nullptr, 10));
    if (const char* q = std::getenv("BS_PROBE_QUALIDADE")) co.quality = uint32_t(std::strtoul(q, nullptr, 10));
    if (!bv::convert_sfd(p, mp4, co, r, &err)) return std::printf("%-14s ERRO conversao: %s\n", p.filename().string().c_str(), err.c_str()), 1;
    if (!bv::verify_conversion(p, mp4, f, &err)) return std::printf("%-14s ERRO verificacao: %s\n", p.filename().string().c_str(), err.c_str()), 1;
    std::printf("%-14s %5u quadros (mp4 %5u) PSNR min %5.2f med %5.2f <40dB %4u SSIM min %.4f med %.4f | audio atraso %d amostras SNR %5.2f dB (mp4 %lld amostras, adx %u) | %lld KB\n",
                p.filename().string().c_str(), f.framesRef, f.framesMp4, f.psnrMin, f.psnrAvg, f.framesBelow40, f.ssimMin, f.ssimAvg,
                f.audioDelaySamples, f.audioSnr, static_cast<long long>(f.mp4AudioSamples), r.audioSamples,
                static_cast<long long>(fs::file_size(mp4) / 1024));
    std::printf("               YUV (so o H.264): Y min %5.2f med %5.2f, CbCr med %5.2f%s%s\n", f.psnrYMin, f.psnrYAvg, f.psnrUvAvg,
                r.encoderNotes.empty() ? "" : " | codificador: ", r.encoderNotes.c_str());
    return f.framesRef == f.framesMp4 ? 0 : 1;
}

static int probe(const fs::path& p) {
    if (!g_out.empty()) return convert(p);
    std::ifstream        f(p, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    bv::SfdStreams       s;
    std::string          err;
    if (!bv::demux_sfd(d, s, &err)) {
        std::printf("%-14s ERRO demux: %s\n", p.filename().string().c_str(), err.c_str());
        return 1;
    }
    uint32_t pics = 0, w = 0, h = 0, rateCode = 0;
    for (size_t i = 0; i + 4 <= s.video.size(); ++i) {
        if (s.video[i] || s.video[i + 1] || s.video[i + 2] != 1) continue;
        if (s.video[i + 3] == 0x00) ++pics;
        if (s.video[i + 3] == 0xB3 && !w && i + 8 <= s.video.size()) {
            w = uint32_t(s.video[i + 4]) << 4 | s.video[i + 5] >> 4;
            h = uint32_t(s.video[i + 5] & 0xF) << 8 | s.video[i + 6];
            rateCode = s.video[i + 7] & 0xF;
        }
    }
    static const double kRates[] = {0, 23.976, 24, 25, 29.97, 30, 50, 59.94, 60};
    const double        fps = rateCode < 9 ? kRates[rateCode] : 0;
    if (const char* raw = std::getenv("BS_PROBE_RAW")) dumpRaw(s, raw);
    std::vector<int16_t> pcm;
    bv::AdxInfo          a;
    const bool           ok = bv::adx_decode(s.audio, pcm, &a, &err);
    int                  peak = 0;
    size_t               clip = 0;
    for (int16_t v : pcm) peak = std::max(peak, std::abs(int(v))), clip += (v == 32767 || v == -32768);
    if (const char* dump = std::getenv("BS_PROBE_DUMP"); dump && ok) {  // ADX extraido e PCM s16le (comparacao externa)
        const fs::path base = fs::path(dump) / p.stem();
        std::ofstream(base.string() + ".adx", std::ios::binary).write(reinterpret_cast<const char*>(s.audio.data()), std::streamsize(s.audio.size()));
        std::ofstream(base.string() + ".s16", std::ios::binary).write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(pcm.size() * 2));
    }
    std::printf("%-14s %ux%u %.2f fps %5u quadros = %7.2f s | ADX %s %u Hz %u ch %8u amostras = %7.2f s pico %5d saturadas %zu | packs %u\n",
                p.filename().string().c_str(), w, h, fps, pics, fps ? pics / fps : 0.0, ok ? "ok" : err.c_str(), a.sampleRate,
                a.channels, a.totalSamples, a.sampleRate ? double(a.totalSamples) / a.sampleRate : 0.0, peak, clip, s.packs);
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--builder") {  // o passo do Builder: <BSDATA> <data/video>
        bv::MovieBuildOptions o;
        o.log      = [](const std::string& m) { std::printf("%s\n", m.c_str()); };
        o.progress = [](double f, const std::string& w) { std::printf("[%3d%%] %s\n", int(f * 100), w.c_str()); };
        bv::MovieBuildResult r;
        std::string          err;
        const bool           ok = bv::build_movies(argv[2], argv[3], o, r, &err);
        std::printf("%s: %d convertidos, %d reaproveitados, %zu ausentes %s\n", ok ? "ok" : "ERRO", r.converted, r.reused, r.absent.size(), err.c_str());
        return ok ? 0 : 1;
    }
    int rc = 0, i = 1;
    if (argc > 2 && std::string(argv[1]) == "--converter") g_out = argv[2], i = 3, fs::create_directories(g_out);
    for (; i < argc; ++i) {
        const fs::path p = argv[i];
        if (fs::is_directory(p)) {
            std::vector<fs::path> files;
            for (const auto& e : fs::directory_iterator(p))
                if (e.path().extension() == ".SFD" || e.path().extension() == ".sfd") files.push_back(e.path());
            std::sort(files.begin(), files.end());
            for (const auto& f : files) rc |= probe(f);
        } else rc |= probe(p);
    }
    return rc;
}
