// N5 (desenvolvimento): confere os filmes do disco sem converter. Para cada .SFD: demux, quadros
// MPEG-1 (codigos de inicio de imagem), duracao do video (30 fps declarados no cabecalho de
// sequencia) e do audio ADX decodificado, e o pico do PCM (saturacao indica decodificacao errada).
//   bs-video-probe <pasta BSDATA ou arquivo .SFD> [...]
//   bs-video-probe --converter <pasta de saida> <pasta BSDATA ou .SFD> [...]: converte em MP4 e mede a
//   fidelidade (PSNR/SSIM por quadro, atraso e SNR do AAC)
//   bs-video-probe --tocar <data\video> [mods\video]: toca os 17 IDs pelo catalogo, sem reconverter
#include "util/hash.h"
#include "video/movie_build.h"
#include "video/movie_catalog.h"
#include "video/movie_playback.h"
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

std::string fileSha(const fs::path& p) {
    std::ifstream     f(p, std::ios::binary);
    bs::util::Sha256  sha;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), std::streamsize(buf.size()));
        sha.update(buf.data(), size_t(f.gcount()));
    }
    return sha.hexdigest();
}

// --tocar <data\video> [mods\video]: os 17 IDs pelo catalogo (com mods), sem reconverter. Confere o
// SHA-256 do MP4 do Builder contra o manifesto e toca cada filme pelo MoviePlayback com o relogio logico
// a 60 Hz e os mesmos parametros do jogo (priming do AAC e fins da fonte). Uma linha por ID (TSV):
// id original origem sha quadros pulados fim_video_ms fim_audio_ms dif_ms amostras fim_ms eof_mp4_ms resultado
// fim_video/fim_audio: os fins que governam o fim no jogo (filme do Builder: os da FONTE, no manifesto;
// mod: os do arquivo). eof_mp4: o fim do audio decodificado do MP4 (diagnostico do preenchimento do AAC).
// O instante do fim (fim_ms) tem que cair no VBlank do mais tardio dos dois (regra LaterOfBoth).
int playAll(const fs::path& dataDir, const fs::path& modsDir) {
    namespace nv          = bs::native::video;
    const auto       cat  = nv::MovieCatalog::load({dataDir, modsDir});
    int              bad  = 0;
    for (const auto& p : cat.problems()) std::printf("# problema no manifesto: %s\n", p.c_str());
    std::printf("id\toriginal\torigem\tsha\tquadros\tpulados\tfim_video_ms\tfim_audio_ms\tdif_ms\tamostras\tfim_ms\teof_mp4_ms\tresultado\n");
    for (uint32_t id = 0; id < 17; ++id) {
        const nv::MovieEntry* e = cat.find(id);
        const std::string     orig = e ? e->original : bv::kMovieNames[id];
        if (!e) {  // nem o manifesto do Builder nem um mod falam deste ID: manifesto faltando ou incompleto
            std::printf("%u\t%s\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\tFORA DO CATALOGO\n", id, orig.c_str());
            ++bad;
            continue;
        }
        if (e->absent) {  // declarado ausente (nao existe no disco, ou nao convertido)
            std::printf("%u\t%s\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\tausente\n", id, orig.c_str());
            continue;
        }
        std::string sha = "-";
        if (!e->mod && !e->sha256.empty()) sha = fileSha(e->file) == e->sha256 ? "ok" : "DIFERENTE";
        nv::PlaybackOptions po;
        if (!e->mod) po.audioSkipFrames = e->audioDelaySamples, po.sourceVideoEnd100ns = e->videoEnd100ns, po.sourceAudioEnd100ns = e->audioEnd100ns;
        nv::MoviePlayback pb;
        std::string       err;
        if (!pb.open(e->file, po, &err)) {
            std::printf("%u\t%s\t%s\t%s\t-\t-\t-\t-\t-\t-\t-\t-\tERRO: %s\n", id, orig.c_str(), e->mod ? "mod" : "builder", sha.c_str(), err.c_str());
            ++bad;
            continue;
        }
        uint64_t samples = 0;
        int64_t  pos = 0, end = -1;
        const int64_t limit = std::max<int64_t>(pb.format().duration100ns, int64_t(e->frames / std::max(1.0, e->fps) * 1e7)) + 100'000'000;
        for (; pos <= limit && err.empty(); pos += 166'667) {  // um VBlank de 60 Hz por consulta
            pb.advance(pos, &err);
            samples += pb.take_audio(pos, &err).size();
            if (pb.finished(pos)) {
                end = pos;
                break;
            }
        }
        const bool    src = !e->mod && e->videoEnd100ns > 0 && e->audioEnd100ns > 0;
        const int64_t ve  = src ? e->videoEnd100ns : pb.video_end100ns();
        const int64_t ae  = src ? e->audioEnd100ns : pb.audio_end100ns();
        // o fim tem que cair na primeira consulta (60 Hz) depois do mais tardio dos dois fins
        const int64_t later  = std::max(ve, ae);
        const bool    endOk  = end >= later && end < later + 166'667;
        const bool    ok = err.empty() && end >= 0 && endOk && sha != "DIFERENTE" &&
                        (e->mod || e->frames == 0 || pb.frames_shown() + pb.frames_skipped() == e->frames);
        bad += !ok;
        std::printf("%u\t%s\t%s\t%s\t%llu\t%llu\t%lld\t%lld\t%lld\t%llu\t%lld\t%lld\t%s\n", id, orig.c_str(), e->mod ? "mod" : "builder",
                    sha.c_str(), static_cast<unsigned long long>(pb.frames_shown()), static_cast<unsigned long long>(pb.frames_skipped()),
                    static_cast<long long>(ve / 10000), static_cast<long long>(ae / 10000), static_cast<long long>((ve - ae) / 10000),
                    static_cast<unsigned long long>(samples / std::max(1u, pb.format().channels)), static_cast<long long>(end / 10000),
                    static_cast<long long>(pb.audio_end100ns() / 10000),
                    ok ? "ok" : (err.empty() ? (endOk ? "FALHA" : "FALHA (instante do fim)") : ("ERRO: " + err).c_str()));
    }
    return bad ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--tocar") return playAll(argv[2], argc >= 4 ? argv[3] : "");
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
