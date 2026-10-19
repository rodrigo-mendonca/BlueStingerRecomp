// N0: os cabecalhos de native/ compilam sozinhos (so native/ no include path) e as
// interfaces sao implementaveis e usaveis sem nada do Dreamcast. Implementacoes minimas em
// memoria, so para exercitar os contratos; os provedores reais sao N1-N9.
#include "audio/audio.h"
#include "core/handle.h"
#include "core/result.h"
#include "platform/files.h"
#include "platform/input.h"
#include "platform/saves.h"
#include "platform/time.h"
#include "render/render.h"
#include "video/video.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace bs::native;

namespace {

int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

class MemFiles final : public files::IFileSystem {
public:
    std::map<std::string, std::vector<std::byte>> data;
    std::map<uint32_t, std::string>               open_;
    uint32_t                                      next_ = 1;

    Result<files::FileHandle> open(std::string_view path) override {
        if (!data.count(std::string(path))) return Error::NotFound;
        open_[next_] = std::string(path);
        return files::FileHandle{next_++};
    }
    Result<uint64_t> size(files::FileHandle f) const override {
        const auto it = open_.find(f.value);
        if (it == open_.end()) return Error::InvalidHandle;
        return uint64_t(data.at(it->second).size());
    }
    Result<size_t> read(files::FileHandle f, uint64_t off, std::span<std::byte> out) override {
        const auto it = open_.find(f.value);
        if (it == open_.end()) return Error::InvalidHandle;
        const auto& d = data.at(it->second);
        if (off > d.size()) return Error::OutOfRange;
        const size_t n = std::min<size_t>(out.size(), size_t(d.size() - off));
        std::memcpy(out.data(), d.data() + off, n);
        return n;
    }
    void close(files::FileHandle f) override { open_.erase(f.value); }
    Result<files::RequestId> read_async(files::FileHandle f, uint64_t off, std::span<std::byte> out) override {
        const auto r = read(f, off, out);
        if (!r) return r.error();
        last_ = {files::RequestState::Done, r.value(), Error::None};
        return files::RequestId{1};
    }
    files::RequestStatus poll(files::RequestId) override { return last_; }
    void                 cancel(files::RequestId) override {}
    Result<std::vector<files::DirEntry>> list(std::string_view) const override {
        std::vector<files::DirEntry> v;
        for (const auto& [n, d] : data) v.push_back({n, d.size(), false});
        return v;
    }

private:
    files::RequestStatus last_;
};

class RecordingDevice final : public render::IRenderDevice {
public:
    size_t frames = 0, draws = 0;
    Result<render::TextureHandle> create_texture(const render::TextureDesc& d, std::span<const std::byte>) override {
        if (!d.width || !d.height) return Error::Unsupported;
        return render::TextureHandle{7};
    }
    Result<void> update_texture(render::TextureHandle, std::span<const std::byte>) override { return {}; }
    void         set_palette(std::span<const uint32_t>) override {}
    void         destroy_texture(render::TextureHandle) override {}
    Result<void> submit(const render::CommandList& l) override {
        ++frames;
        draws += l.draws.size();
        return {};
    }
    Result<void> present_image(std::span<const uint32_t>, uint32_t, uint32_t) override { return {}; }
    Result<void> present() override { return {}; }
    void         resize(uint32_t, uint32_t) override {}
};

} // namespace

int main() {
    // Result e Handle
    Result<int> ok = 3, bad = Error::NotFound;
    CHECK(ok && ok.value() == 3 && !bad && bad.error() == Error::NotFound);
    CHECK(std::string(error_name(Error::Busy)) == "ocupado");
    files::FileHandle none;
    CHECK(!none && files::FileHandle{5} == files::FileHandle{5});

    MemFiles fs;
    const char text[] = "BLUE STINGER";
    fs.data["\\BSDATA\\CNF00.NT"].assign(reinterpret_cast<const std::byte*>(text),
                                        reinterpret_cast<const std::byte*>(text) + 12);
    files::IFileSystem& ifs = fs;
    auto                h   = ifs.open("\\BSDATA\\CNF00.NT");
    CHECK(h && ifs.size(h.value()).value() == 12);
    std::byte buf[4];
    CHECK(ifs.read(h.value(), 5, buf).value() == 4 && std::memcmp(buf, "STIN", 4) == 0);
    auto rq = ifs.read_async(h.value(), 0, buf);
    CHECK(rq && ifs.poll(rq.value()).state == files::RequestState::Done);
    ifs.close(h.value());
    CHECK(ifs.size(h.value()).error() == Error::InvalidHandle);
    CHECK(ifs.open("\\NAO\\EXISTE").error() == Error::NotFound);

    RecordingDevice      dev;
    render::IRenderDevice& rd = dev;
    render::CommandList  frame;
    frame.vertices.resize(4);
    render::Draw d;
    d.state.list    = render::ListType::Translucent;
    d.state.texture = rd.create_texture({256, 256, render::PixelFormat::ARGB1555, 9}, {}).value();
    d.vertex_count  = 4;
    frame.draws.push_back(d);
    CHECK(rd.submit(frame) && dev.frames == 1 && dev.draws == 1);
    CHECK(rd.create_texture({0, 0}, {}).error() == Error::Unsupported);

    input::ControllerState pad;
    pad.buttons = input::A | input::Start;
    pad.axis(input::Axis::StickY) = 1.0f;
    CHECK((pad.buttons & input::Start) && !(pad.buttons & input::B) && pad.axis(input::Axis::StickY) == 1.0f);
    saves::SaveEntry si{"BLUESTIN.000", 4, 0};
    CHECK(si.blocks == 4 && saves::valid_save_name(si.name));
    constexpr time::TimeBase tb;  // campo NTSC exato
    CHECK(tb.frame(tb.frame_start(120)) == 120);
    static_assert(tb.ns(tb.frame_start(60000)) == 1001000000000ull);
    constexpr time::Rate ntsc;
    static_assert(ntsc.frame_ns(60000) == 1001000000000ull);  // 60 000 campos = 1 001 s exatos
    CHECK(ntsc.frame_ns(1) == 16683333);
    audio::VoiceParams vp;
    CHECK(vp.volume == 1.0f && vp.pitch == 1.0f);
    video::MovieInfo mi;
    CHECK(!mi.has_audio);

    if (g_fail) return 1;
    std::printf("native_api: ok\n");
    return 0;
}
