#include "video/mf_movie.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "core/com_scope.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <initguid.h>  // so para os GUIDs do codecapi.h abaixo (os do MF vem do mfuuid)
#include <codecapi.h>
#include <strmif.h>

#include <cstring>
#include <mutex>

namespace bs::native::video {

namespace {

template <class T>
struct Com {  // ponteiro COM minimo (sem ATL/WRL)
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release(), p = nullptr;
    }
    T** put() { reset(); return &p; }
    T*  operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::string hr(const char* what, HRESULT h) {
    char b[160];
    std::snprintf(b, sizeof b, "%s (HRESULT 0x%08lX)", what, static_cast<unsigned long>(h));
    return b;
}
bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

// MFStartup por contagem de referencias (leitores e gravadores podem coexistir).
std::mutex g_mfMutex;
int        g_mfUsers = 0;
bool mfAcquire(std::string* err) {
    std::lock_guard lk(g_mfMutex);
    if (g_mfUsers == 0) {
        if (const HRESULT h = MFStartup(MF_VERSION, MFSTARTUP_LITE); FAILED(h))
            return fail(err, hr("Media Foundation indisponivel (MFStartup; no Windows N falta o Media Feature Pack)", h));
    }
    ++g_mfUsers;
    return true;
}
void mfRelease() {
    std::lock_guard lk(g_mfMutex);
    if (--g_mfUsers == 0) MFShutdown();
}

constexpr DWORD kVideo = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAudio = DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

} // namespace

// ---------------------------------------------------------------------------- leitor

struct MfMovieReader::Impl {
    ComScope              com;  // antes do MF (destruido por ultimo)
    bool                  mf = false;
    Com<IMFSourceReader>  reader;
    LONG                  stride = 0;
    bool                  audio = false;
    bool                  nv12 = false;  // o decodificador entrega NV12 e a conversao para RGB e nossa
    std::vector<uint8_t>  yuv;
    ~Impl() {
        reader.reset();
        if (mf) mfRelease();
    }
};

std::unique_ptr<IMovieDecoder> make_movie_decoder() { return std::make_unique<MfMovieReader>(); }

MfMovieReader::MfMovieReader() = default;
MfMovieReader::~MfMovieReader() { close(); }

void MfMovieReader::close() {
    impl_.reset();
    fmt_ = {};
}

bool MfMovieReader::open(const std::filesystem::path& file, std::string* err, bool nv12) {
    close();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return fail(err, "filme nao encontrado: " + file.string());
    auto im = std::make_unique<Impl>();
    if (!mfAcquire(err)) return false;
    im->mf = true;
    Com<IMFAttributes> at;
    MFCreateAttributes(at.put(), 2);
    at->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);  // so para o RGB32 de reserva
    if (const HRESULT h = MFCreateSourceReaderFromURL(file.c_str(), at.p, im->reader.put()); FAILED(h))
        return fail(err, hr(("arquivo de filme invalido ou sem decodificador: " + file.filename().string()).c_str(), h));
    // Video em NV12 (o formato nativo dos decodificadores): a conversao para RGB e a nossa (BT.601 de
    // faixa de TV, croma replicado), igual em qualquer GPU e igual a referencia do Builder. Se o
    // decodificador de um mod nao entregar NV12, RGB32 pelo processador de video do MF.
    Com<IMFMediaType> vt;
    MFCreateMediaType(vt.put());
    vt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    im->nv12 = SUCCEEDED(im->reader->SetCurrentMediaType(kVideo, nullptr, vt.p));
    if (!im->nv12) {
        if (nv12) return fail(err, "o decodificador deste filme nao entrega NV12");
        vt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (const HRESULT h = im->reader->SetCurrentMediaType(kVideo, nullptr, vt.p); FAILED(h))
            return fail(err, hr("o filme nao tem video decodificavel", h));
    }
    im->reader->SetStreamSelection(kVideo, TRUE);
    Com<IMFMediaType> cur;
    im->reader->GetCurrentMediaType(kVideo, cur.put());
    UINT32 w = 0, hgt = 0, num = 0, den = 1;
    MFGetAttributeSize(cur.p, MF_MT_FRAME_SIZE, &w, &hgt);
    MFGetAttributeRatio(cur.p, MF_MT_FRAME_RATE, &num, &den);
    im->stride = LONG(MFGetAttributeUINT32(cur.p, MF_MT_DEFAULT_STRIDE, im->nv12 ? w : w * 4));
    fmt_.width = w, fmt_.height = hgt, fmt_.fps = den ? double(num) / den : 0;
    // audio em PCM 16 bits (opcional)
    Com<IMFMediaType> atp;
    MFCreateMediaType(atp.put());
    atp->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    atp->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    atp->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(im->reader->SetCurrentMediaType(kAudio, nullptr, atp.p))) {
        im->reader->SetStreamSelection(kAudio, TRUE);
        Com<IMFMediaType> ac;
        im->reader->GetCurrentMediaType(kAudio, ac.put());
        fmt_.hasAudio   = true;
        fmt_.sampleRate = MFGetAttributeUINT32(ac.p, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
        fmt_.channels   = MFGetAttributeUINT32(ac.p, MF_MT_AUDIO_NUM_CHANNELS, 0);
        im->audio       = true;
    }
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(im->reader->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &v)))
        fmt_.duration100ns = int64_t(v.uhVal.QuadPart);
    PropVariantClear(&v);
    impl_ = std::move(im);
    return true;
}

namespace {
uint8_t clamp8(int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }
} // namespace

bool MfMovieReader::next_frame(VideoFrame& f, std::string* err) {
    if (!impl_) return fail(err, "filme fechado");
    if (impl_->nv12) {  // NV12 -> ARGB, BT.601 faixa de TV, a mesma aritmetica da referencia (pl_mpeg)
        if (!next_frame_nv12(impl_->yuv, f.time100ns, err)) return false;
        const uint32_t w = fmt_.width, h = fmt_.height;
        f.width = w, f.height = h, f.duration100ns = fmt_.fps > 0 ? int64_t(1e7 / fmt_.fps + 0.5) : 0;
        f.argb.resize(size_t(w) * h);
        const uint8_t* Y  = impl_->yuv.data();
        const uint8_t* UV = Y + size_t(w) * h;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const int cb = int(UV[size_t(y / 2) * w + (x & ~1u)]) - 128, cr = int(UV[size_t(y / 2) * w + (x & ~1u) + 1]) - 128;
                const int r = (cr * 104597) >> 16, g = (cb * 25674 + cr * 53278) >> 16, b = (cb * 132201) >> 16;
                const int l = ((int(Y[size_t(y) * w + x]) - 16) * 76309) >> 16;
                f.argb[size_t(y) * w + x] = 0xFF000000u | uint32_t(clamp8(l + r)) << 16 | uint32_t(clamp8(l - g)) << 8 | clamp8(l + b);
            }
        return true;
    }
    for (;;) {
        DWORD             flags = 0;
        LONGLONG          ts    = 0;
        Com<IMFSample>    s;
        if (const HRESULT h = impl_->reader->ReadSample(kVideo, 0, nullptr, &flags, &ts, s.put()); FAILED(h))
            return fail(err, hr("erro ao decodificar o video", h));
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            Com<IMFMediaType> cur;
            impl_->reader->GetCurrentMediaType(kVideo, cur.put());
            UINT32 w = 0, hgt = 0;
            MFGetAttributeSize(cur.p, MF_MT_FRAME_SIZE, &w, &hgt);
            impl_->stride = LONG(MFGetAttributeUINT32(cur.p, MF_MT_DEFAULT_STRIDE, w * 4));
            fmt_.width = w, fmt_.height = hgt;
        }
        if (!s) continue;  // lacuna no stream
        Com<IMFMediaBuffer> buf;
        s->ConvertToContiguousBuffer(buf.put());
        BYTE* data = nullptr;
        DWORD len  = 0;
        if (FAILED(buf->Lock(&data, nullptr, &len))) return fail(err, "quadro de video sem dados");
        const uint32_t w = fmt_.width, hgt = fmt_.height;
        const LONG     stride = impl_->stride ? impl_->stride : LONG(w * 4);
        const size_t   absStride = size_t(stride < 0 ? -stride : stride);
        if (absStride < size_t(w) * 4 || size_t(len) < absStride * hgt) {
            buf->Unlock();
            return fail(err, "quadro de video menor que o formato declarado");
        }
        f.width = w, f.height = hgt;
        f.argb.resize(size_t(w) * hgt);
        for (uint32_t y = 0; y < hgt; ++y) {
            const BYTE* row = stride > 0 ? data + absStride * y : data + absStride * (hgt - 1 - y);
            uint32_t*   dst = f.argb.data() + size_t(y) * w;
            std::memcpy(dst, row, size_t(w) * 4);
            for (uint32_t x = 0; x < w; ++x) dst[x] |= 0xFF000000u;  // RGB32 nao garante o alfa
        }
        buf->Unlock();
        LONGLONG dur = 0;
        s->GetSampleDuration(&dur);
        f.time100ns = ts, f.duration100ns = dur;
        return true;
    }
}

bool MfMovieReader::next_frame_nv12(std::vector<uint8_t>& out, int64_t& time100ns, std::string* err) {
    if (!impl_) return fail(err, "filme fechado");
    if (!impl_->nv12) return fail(err, "filme sem saida NV12");
    for (;;) {
        DWORD          flags = 0;
        LONGLONG       ts    = 0;
        Com<IMFSample> s;
        if (const HRESULT h = impl_->reader->ReadSample(kVideo, 0, nullptr, &flags, &ts, s.put()); FAILED(h))
            return fail(err, hr("erro ao decodificar o video", h));
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
        if (!s) continue;
        Com<IMFMediaBuffer> buf;
        s->ConvertToContiguousBuffer(buf.put());
        BYTE* data = nullptr;
        DWORD len  = 0;
        if (FAILED(buf->Lock(&data, nullptr, &len))) return fail(err, "quadro de video sem dados");
        const uint32_t w = fmt_.width, h = fmt_.height;
        const size_t   stride = size_t(impl_->stride > 0 ? impl_->stride : LONG(w));
        // o decodificador pode alinhar a altura do plano Y (p.ex. 240 -> 256 linhas)
        const size_t yRows = len >= stride * h * 3 / 2 ? (len * 2 / 3) / stride : h;
        if (size_t(len) < stride * yRows + stride * (h / 2)) {
            buf->Unlock();
            return fail(err, "quadro NV12 menor que o formato");
        }
        out.resize(size_t(w) * h * 3 / 2);
        for (uint32_t y = 0; y < h; ++y) std::memcpy(out.data() + size_t(y) * w, data + stride * y, w);
        for (uint32_t y = 0; y < h / 2; ++y) std::memcpy(out.data() + size_t(w) * h + size_t(y) * w, data + stride * (yRows + y), w);
        buf->Unlock();
        time100ns = ts;
        return true;
    }
}

bool MfMovieReader::next_audio(std::vector<int16_t>& pcm, int64_t& time100ns, std::string* err) {
    if (!impl_ || !impl_->audio) return false;
    for (;;) {
        DWORD          flags = 0;
        LONGLONG       ts    = 0;
        Com<IMFSample> s;
        if (const HRESULT h = impl_->reader->ReadSample(kAudio, 0, nullptr, &flags, &ts, s.put()); FAILED(h))
            return fail(err, hr("erro ao decodificar o audio do filme", h));
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
        if (!s) continue;
        Com<IMFMediaBuffer> buf;
        s->ConvertToContiguousBuffer(buf.put());
        BYTE* data = nullptr;
        DWORD len  = 0;
        if (FAILED(buf->Lock(&data, nullptr, &len))) return fail(err, "bloco de audio sem dados");
        pcm.resize(len / 2);
        std::memcpy(pcm.data(), data, pcm.size() * 2);
        buf->Unlock();
        time100ns = ts;
        return true;
    }
}

// ---------------------------------------------------------------------------- gravador

struct MfMovieWriter::Impl {
    ComScope           com;
    bool               mf = false;
    Com<IMFSinkWriter> w;
    DWORD              vs = 0, as = 0;
    WriterOptions      o;
    uint64_t           frames = 0, samples = 0;
    ~Impl() {
        w.reset();
        if (mf) mfRelease();
    }
};

MfMovieWriter::MfMovieWriter() = default;
MfMovieWriter::~MfMovieWriter() = default;

bool MfMovieWriter::open(const std::filesystem::path& file, const WriterOptions& o, std::string* err) {
    impl_.reset();
    if (o.width % 2 || o.height % 2 || !o.fpsNum || !o.fpsDen) return fail(err, "formato de video invalido para H.264");
    auto im = std::make_unique<Impl>();
    if (!mfAcquire(err)) return false;
    im->mf = true;
    im->o  = o;
    Com<IMFAttributes> at;
    MFCreateAttributes(at.put(), 1);
    at->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);  // codificador de software: mesmo resultado em qualquer GPU
    if (const HRESULT h = MFCreateSinkWriterFromURL(file.c_str(), nullptr, at.p, im->w.put()); FAILED(h))
        return fail(err, hr("nao foi possivel criar o MP4", h));
    // video: H.264 na saida, RGB32 na entrada
    Com<IMFMediaType> out, in;
    MFCreateMediaType(out.put());
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, o.videoBitrate);
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out.p, MF_MT_FRAME_SIZE, o.width, o.height);
    MFSetAttributeRatio(out.p, MF_MT_FRAME_RATE, o.fpsNum, o.fpsDen);
    MFSetAttributeRatio(out.p, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (o.nv12Input) {  // sinaliza a matriz e a faixa no H.264 (o decodificador volta pelo mesmo BT.601)
        out->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601);
        out->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    }
    if (const HRESULT h = im->w->AddStream(out.p, &im->vs); FAILED(h)) return fail(err, hr("codificador H.264 indisponivel", h));
    MFCreateMediaType(in.put());
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, o.nv12Input ? MFVideoFormat_NV12 : MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    in->SetUINT32(MF_MT_DEFAULT_STRIDE, o.nv12Input ? o.width : o.width * 4);  // de cima para baixo
    if (o.nv12Input) {  // o YUV da fonte MPEG-1: BT.601, faixa de TV
        in->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601);
        in->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    }
    MFSetAttributeSize(in.p, MF_MT_FRAME_SIZE, o.width, o.height);
    MFSetAttributeRatio(in.p, MF_MT_FRAME_RATE, o.fpsNum, o.fpsDen);
    MFSetAttributeRatio(in.p, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    // parametros do codificador na criacao (o caminho documentado do SinkWriter; o ICodecAPI depois
    // da criacao e aceito mas ignorado pelo H.264 de software do Windows)
    Com<IMFAttributes> enc;
    MFCreateAttributes(enc.put(), 3);
    enc->SetUINT32(CODECAPI_AVEncCommonRateControlMode,
                   o.quality ? eAVEncCommonRateControlMode_Quality : eAVEncCommonRateControlMode_UnconstrainedVBR);
    if (o.quality) enc->SetUINT32(CODECAPI_AVEncCommonQuality, o.quality);
    else enc->SetUINT32(CODECAPI_AVEncCommonMeanBitRate, o.videoBitrate);
    if (const HRESULT h = im->w->SetInputMediaType(im->vs, in.p, enc.p); FAILED(h)) return fail(err, hr("entrada de video recusada pelo H.264", h));
    if (o.audio) {
        Com<IMFMediaType> aout, ain;
        MFCreateMediaType(aout.put());
        aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        aout->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, o.sampleRate);
        aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, o.channels);
        aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, o.audioBytesPerSecond);
        if (const HRESULT h = im->w->AddStream(aout.p, &im->as); FAILED(h)) return fail(err, hr("codificador AAC indisponivel", h));
        MFCreateMediaType(ain.put());
        ain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        ain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        ain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        ain->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, o.sampleRate);
        ain->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, o.channels);
        ain->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, o.channels * 2);
        ain->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, o.sampleRate * o.channels * 2);
        if (const HRESULT h = im->w->SetInputMediaType(im->as, ain.p, nullptr); FAILED(h)) return fail(err, hr("entrada PCM recusada pelo AAC", h));
    }
    // controle de taxa do H.264 (sem isto o codificador do Windows ignora o MF_MT_AVG_BITRATE)
    {
        Com<ICodecAPI> api;
        if (SUCCEEDED(im->w->GetServiceForStream(im->vs, GUID_NULL, IID_PPV_ARGS(api.put())))) {
            VARIANT v;
            VariantInit(&v);
            v.vt   = VT_UI4;
            v.ulVal = o.quality ? eAVEncCommonRateControlMode_Quality : eAVEncCommonRateControlMode_UnconstrainedVBR;
            const HRESULT h1 = api->SetValue(&CODECAPI_AVEncCommonRateControlMode, &v);
            v.ulVal = o.quality ? o.quality : o.videoBitrate;
            const HRESULT h2 = api->SetValue(o.quality ? &CODECAPI_AVEncCommonQuality : &CODECAPI_AVEncCommonMeanBitRate, &v);
            if (FAILED(h1)) notes_ += hr("modo de controle de taxa recusado", h1) + "; ";
            if (FAILED(h2)) notes_ += hr("qualidade/bitrate recusado", h2) + "; ";
        } else notes_ += "codificador sem ICodecAPI (controle de taxa padrao); ";
    }
    if (const HRESULT h = im->w->BeginWriting(); FAILED(h)) return fail(err, hr("nao foi possivel iniciar a gravacao do MP4", h));
    impl_ = std::move(im);
    return true;
}

namespace {
bool writeBuffer(IMFSinkWriter* w, DWORD stream, const void* data, size_t bytes, LONGLONG t, LONGLONG d, std::string* err) {
    Com<IMFMediaBuffer> b;
    if (FAILED(MFCreateMemoryBuffer(DWORD(bytes), b.put()))) return fail(err, "sem memoria para o quadro");
    BYTE* p = nullptr;
    b->Lock(&p, nullptr, nullptr);
    std::memcpy(p, data, bytes);
    b->Unlock();
    b->SetCurrentLength(DWORD(bytes));
    Com<IMFSample> s;
    MFCreateSample(s.put());
    s->AddBuffer(b.p);
    s->SetSampleTime(t);
    s->SetSampleDuration(d);
    if (const HRESULT h = w->WriteSample(stream, s.p); FAILED(h)) return fail(err, hr("erro ao codificar", h));
    return true;
}
} // namespace

bool MfMovieWriter::write_frame(std::span<const uint32_t> argb, std::string* err) {
    if (!impl_) return fail(err, "gravador fechado");
    const auto& o = impl_->o;
    if (argb.size() != size_t(o.width) * o.height) return fail(err, "quadro com tamanho diferente do formato");
    const LONGLONG t = LONGLONG(impl_->frames * 10'000'000ull * o.fpsDen / o.fpsNum);
    const LONGLONG n = LONGLONG((impl_->frames + 1) * 10'000'000ull * o.fpsDen / o.fpsNum);
    ++impl_->frames;
    return writeBuffer(impl_->w.p, impl_->vs, argb.data(), argb.size() * 4, t, n - t, err);
}

bool MfMovieWriter::write_frame_nv12(std::span<const uint8_t> nv12, std::string* err) {
    if (!impl_ || !impl_->o.nv12Input) return fail(err, "gravador sem entrada NV12");
    const auto& o = impl_->o;
    if (nv12.size() != size_t(o.width) * o.height * 3 / 2) return fail(err, "quadro NV12 com tamanho diferente do formato");
    const LONGLONG t = LONGLONG(impl_->frames * 10'000'000ull * o.fpsDen / o.fpsNum);
    const LONGLONG n = LONGLONG((impl_->frames + 1) * 10'000'000ull * o.fpsDen / o.fpsNum);
    ++impl_->frames;
    return writeBuffer(impl_->w.p, impl_->vs, nv12.data(), nv12.size(), t, n - t, err);
}

bool MfMovieWriter::write_audio(std::span<const int16_t> pcm, std::string* err) {
    if (!impl_ || !impl_->o.audio) return fail(err, "gravador sem audio");
    const auto&    o = impl_->o;
    const uint64_t n = pcm.size() / o.channels;
    const LONGLONG t = LONGLONG(impl_->samples * 10'000'000ull / o.sampleRate);
    impl_->samples += n;
    const LONGLONG e = LONGLONG(impl_->samples * 10'000'000ull / o.sampleRate);
    return writeBuffer(impl_->w.p, impl_->as, pcm.data(), pcm.size() * 2, t, e - t, err);
}

bool MfMovieWriter::finish(std::string* err) {
    if (!impl_) return fail(err, "gravador fechado");
    const HRESULT h = impl_->w->Finalize();
    impl_.reset();
    return SUCCEEDED(h) || fail(err, hr("erro ao finalizar o MP4", h));
}

} // namespace bs::native::video
