#include "video/sfd_convert.h"

#include "video/mf_movie.h"
#include "video/sfd_media.h"

#include <cstdio>
extern "C" {
#include "pl_mpeg.h"
plm_frame_t* bs_plm_video_flush(plm_video_t* self);  // pl_mpeg_impl.c
}

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>

namespace bs::builder::video {

namespace fs = std::filesystem;
namespace nv = bs::native::video;

namespace {

bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

bool readFile(const fs::path& p, std::vector<uint8_t>& d) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    d.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// Decodificador MPEG-1 (pl_mpeg) sobre o elementary stream ja separado pelo demux.
struct Mpeg1 {
    std::vector<uint8_t> es;
    plm_video_t*         v = nullptr;
    explicit Mpeg1(std::vector<uint8_t> bytes) : es(std::move(bytes)) {
        plm_buffer_t* b = plm_buffer_create_with_memory(es.data(), es.size(), 0);
        plm_buffer_signal_end(b);
        v = plm_video_create_with_buffer(b, 1);
    }
    ~Mpeg1() {
        if (v) plm_video_destroy(v);
    }
    plm_frame_t* next() {
        plm_frame_t* f = plm_video_decode(v);
        return f ? f : bs_plm_video_flush(v);  // os ultimos quadros (defeitos de fim do pl_mpeg)
    }
};

void toNv12(const plm_frame_t* f, std::vector<uint8_t>& out) {
    const uint32_t w = f->width, h = f->height;
    out.resize(size_t(w) * h * 3 / 2);
    for (uint32_t y = 0; y < h; ++y) std::copy_n(f->y.data + size_t(y) * f->y.width, w, out.data() + size_t(y) * w);
    uint8_t* uv = out.data() + size_t(w) * h;
    for (uint32_t y = 0; y < h / 2; ++y)
        for (uint32_t x = 0; x < w / 2; ++x) {
            uv[size_t(y) * w + 2 * x]     = f->cb.data[size_t(y) * f->cb.width + x];  // U
            uv[size_t(y) * w + 2 * x + 1] = f->cr.data[size_t(y) * f->cr.width + x];  // V
        }
}

void toArgb(plm_frame_t* f, std::vector<uint32_t>& out) {
    out.resize(size_t(f->width) * f->height);
    plm_frame_to_bgra(f, reinterpret_cast<uint8_t*>(out.data()), int(f->width) * 4);  // B,G,R,A = 0xAARRGGBB
    for (auto& p : out) p |= 0xFF000000u;
}

double psnrRgb(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    double se = 0;
    for (size_t i = 0; i < a.size(); ++i)
        for (int sh : {16, 8, 0}) {
            const int d = int((a[i] >> sh) & 0xFF) - int((b[i] >> sh) & 0xFF);
            se += double(d) * d;
        }
    const double mse = se / (double(a.size()) * 3);
    return mse <= 1e-12 ? 99.0 : 10 * std::log10(255.0 * 255.0 / mse);
}

// SSIM da luma em janelas 8x8 sem sobreposicao (constantes de Wang et al.)
double ssimLuma(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, uint32_t w, uint32_t h) {
    auto luma = [](uint32_t p) { return 0.299 * ((p >> 16) & 0xFF) + 0.587 * ((p >> 8) & 0xFF) + 0.114 * (p & 0xFF); };
    const double c1 = std::pow(0.01 * 255, 2), c2 = std::pow(0.03 * 255, 2);
    double       sum = 0;
    int          n   = 0;
    for (uint32_t by = 0; by + 8 <= h; by += 8)
        for (uint32_t bx = 0; bx + 8 <= w; bx += 8) {
            double ma = 0, mb = 0, va = 0, vb = 0, cov = 0;
            for (uint32_t y = by; y < by + 8; ++y)
                for (uint32_t x = bx; x < bx + 8; ++x) ma += luma(a[size_t(y) * w + x]), mb += luma(b[size_t(y) * w + x]);
            ma /= 64, mb /= 64;
            for (uint32_t y = by; y < by + 8; ++y)
                for (uint32_t x = bx; x < bx + 8; ++x) {
                    const double da = luma(a[size_t(y) * w + x]) - ma, db = luma(b[size_t(y) * w + x]) - mb;
                    va += da * da, vb += db * db, cov += da * db;
                }
            va /= 63, vb /= 63, cov /= 63;
            sum += ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
            ++n;
        }
    return n ? sum / n : 1.0;
}

} // namespace

bool convert_sfd(const fs::path& sfd, const fs::path& mp4, const ConvertOptions& o, ConvertResult& r, std::string* err) {
    r = {};
    if (o.testNoEncoder) return fail(err, "codificador H.264 indisponivel (simulado)");
    std::vector<uint8_t> file;
    if (!readFile(sfd, file)) return fail(err, "nao abre " + sfd.string());
    SfdStreams s;
    if (!demux_sfd(file, s, err)) return false;
    std::vector<int16_t> pcm;
    AdxInfo              a;
    if (!adx_decode(s.audio, pcm, &a, err)) return false;
    Mpeg1 m(std::move(s.video));
    if (!m.v) return fail(err, "MPEG-1 sem cabecalho de sequencia");
    plm_frame_t* f = m.next();
    if (!f) return fail(err, "MPEG-1 sem quadros");
    r.width = f->width, r.height = f->height, r.fps = plm_video_get_framerate(m.v);
    const uint32_t fpsNum = uint32_t(std::lround(r.fps * 1000)), fpsDen = 1000;
    nv::WriterOptions wo;
    wo.width = r.width, wo.height = r.height, wo.fpsNum = fpsNum, wo.fpsDen = fpsDen;
    wo.videoBitrate = o.videoBitrate, wo.quality = o.quality, wo.nv12Input = true;
    wo.sampleRate = a.sampleRate, wo.channels = a.channels, wo.audioBytesPerSecond = o.audioBytesPerSecond;
    nv::MfMovieWriter w;
    if (!w.open(mp4, wo, err)) return false;
    std::vector<uint8_t> nv12;
    // audio intercalado com o video (blocos de ~1 quadro), como o multiplexador espera
    const size_t perFrame = size_t(std::ceil(a.sampleRate / r.fps)) * a.channels;
    size_t       ap       = 0;
    for (; f; f = m.next()) {
        toNv12(f, nv12);
        if (!w.write_frame_nv12(nv12, err)) return false;
        ++r.frames;
        const size_t n = std::min(perFrame, pcm.size() - ap);
        if (n && !w.write_audio(std::span<const int16_t>(pcm.data() + ap, n), err)) return false;
        ap += n;
    }
    if (ap < pcm.size() && !w.write_audio(std::span<const int16_t>(pcm.data() + ap, pcm.size() - ap), err)) return false;
    r.encoderNotes = w.notes();
    if (!w.finish(err)) return false;
    r.videoEnd100ns = int64_t(std::llround(r.frames * 1e7 / r.fps));
    r.sampleRate = a.sampleRate, r.channels = a.channels, r.audioSamples = a.totalSamples;
    r.audioEnd100ns = int64_t(a.totalSamples) * 10'000'000 / a.sampleRate;
    return true;
}

bool verify_conversion(const fs::path& sfd, const fs::path& mp4, Fidelity& fd, std::string* err) {
    fd = {};
    std::vector<uint8_t> file;
    if (!readFile(sfd, file)) return fail(err, "nao abre " + sfd.string());
    SfdStreams s;
    if (!demux_sfd(file, s, err)) return false;
    std::vector<int16_t> ref;
    AdxInfo              a;
    if (!adx_decode(s.audio, ref, &a, err)) return false;
    Mpeg1             m(std::move(s.video));
    nv::MfMovieReader rd;
    if (!rd.open(mp4, err)) return false;
    std::vector<uint32_t> want;
    nv::VideoFrame        got;
    double                psum = 0, ssum = 0;
    fd.psnrMin = 1e9, fd.ssimMin = 1e9;
    for (plm_frame_t* f = m.next(); f; f = m.next()) {
        toArgb(f, want);
        ++fd.framesRef;
        if (!rd.next_frame(got, err)) break;
        ++fd.framesMp4;
        if (got.width != f->width || got.height != f->height) return fail(err, "MP4 com dimensoes diferentes da fonte");
        const double p = psnrRgb(want, got.argb), q = ssimLuma(want, got.argb, f->width, f->height);
        psum += p, ssum += q;
        fd.psnrMin = std::min(fd.psnrMin, p), fd.ssimMin = std::min(fd.ssimMin, q);
        fd.framesBelow40 += p < 40.0;
    }
    while (rd.next_frame(got)) ++fd.framesMp4;
    if (fd.framesMp4) fd.psnrAvg = psum / fd.framesMp4, fd.ssimAvg = ssum / fd.framesMp4;
    // planos YUV (so a perda do H.264)
    {
        std::vector<uint8_t> f2;
        if (!readFile(sfd, f2)) return fail(err, "nao abre " + sfd.string());
        SfdStreams s2;
        demux_sfd(f2, s2, nullptr);
        Mpeg1             m2(std::move(s2.video));
        nv::MfMovieReader yuv;
        if (!yuv.open(mp4, err, true)) return false;
        std::vector<uint8_t> ref, got2;
        int64_t              ts = 0;
        double               ysum = 0, uvsum = 0;
        uint32_t             n = 0;
        fd.psnrYMin = 1e9;
        auto plane = [](const uint8_t* a, const uint8_t* b, size_t len) {
            double se = 0;
            for (size_t i = 0; i < len; ++i) se += double(int(a[i]) - int(b[i])) * (int(a[i]) - int(b[i]));
            const double mse = se / double(len);
            return mse <= 1e-12 ? 99.0 : 10 * std::log10(255.0 * 255.0 / mse);
        };
        for (plm_frame_t* f = m2.next(); f && yuv.next_frame_nv12(got2, ts, err); f = m2.next(), ++n) {
            toNv12(f, ref);
            const size_t ys = size_t(f->width) * f->height;
            const double py = plane(ref.data(), got2.data(), ys), pc = plane(ref.data() + ys, got2.data() + ys, ys / 2);
            ysum += py, uvsum += pc, fd.psnrYMin = std::min(fd.psnrYMin, py);
        }
        if (n) fd.psnrYAvg = ysum / n, fd.psnrUvAvg = uvsum / n;
    }
    // audio: atraso pelo maximo da correlacao nos primeiros 2 s (canal esquerdo), SNR depois de alinhado
    std::vector<int16_t> mp4a, blk;
    int64_t              t = 0;
    while (rd.next_audio(blk, t)) mp4a.insert(mp4a.end(), blk.begin(), blk.end());
    const uint32_t ch = std::max<uint32_t>(1, a.channels);
    fd.mp4AudioSamples = int64_t(mp4a.size() / ch);
    const size_t win = std::min<size_t>(size_t(a.sampleRate) * 2, ref.size() / ch);
    double       best = -1e300;
    for (int32_t lag = 0; lag <= 4096; ++lag) {
        double c = 0;
        for (size_t i = 0; i < win && (i + size_t(lag)) * ch < mp4a.size(); i += 2) c += double(ref[i * ch]) * mp4a[(i + size_t(lag)) * ch];
        if (c > best) best = c, fd.audioDelaySamples = lag;
    }
    double sig = 0, noise = 0;
    for (size_t i = 0; i < ref.size() / ch && (i + size_t(fd.audioDelaySamples)) * ch < mp4a.size(); ++i)
        for (uint32_t c = 0; c < ch; ++c) {
            const double x = ref[i * ch + c], y = mp4a[(i + size_t(fd.audioDelaySamples)) * ch + c];
            sig += x * x, noise += (x - y) * (x - y);
        }
    fd.audioSnr = noise > 0 ? 10 * std::log10(sig / noise) : 99.0;
    return true;
}

} // namespace bs::builder::video
