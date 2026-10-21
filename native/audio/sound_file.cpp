#include "audio/sound_file.h"

#include "audio/aica_adpcm.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace bs::native::audio {

namespace {
uint32_t u32(std::span<const uint8_t> b, size_t o) { return uint32_t(b[o]) | b[o + 1] << 8 | b[o + 2] << 16 | uint32_t(b[o + 3]) << 24; }
uint16_t u16(std::span<const uint8_t> b, size_t o) { return uint16_t(b[o] | b[o + 1] << 8); }
std::shared_ptr<SoundData> fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return nullptr;
}
} // namespace

std::shared_ptr<SoundData> parse_wav(std::span<const uint8_t> b, std::string* err) {
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0)
        return fail(err, "nao e um WAV (RIFF/WAVE)");
    uint16_t tag = 0xFFFF, channels = 0, bits = 0;
    uint32_t rate = 0;
    std::span<const uint8_t> data;
    bool                     hasData = false;
    int64_t loopStart = -1, loopEnd = -1;
    for (size_t o = 12; o + 8 <= b.size();) {
        const uint32_t size = u32(b, o + 4);
        if (o + 8 + size > b.size()) return fail(err, "WAV truncado");
        const auto body = b.subspan(o + 8, size);
        if (std::memcmp(b.data() + o, "fmt ", 4) == 0 && size >= 16) {
            tag = u16(body, 0), channels = u16(body, 2), rate = u32(body, 4), bits = u16(body, 14);
        } else if (std::memcmp(b.data() + o, "data", 4) == 0) {
            data = body, hasData = true;
        } else if (std::memcmp(b.data() + o, "smpl", 4) == 0 && size >= 36 + 24 && u32(body, 28) > 0) {
            loopStart = u32(body, 36 + 8), loopEnd = int64_t(u32(body, 36 + 12)) + 1;  // smpl: fim inclusivo
        }
        o += 8 + size + (size & 1);
    }
    if (tag == 0xFFFF || !channels || !rate) return fail(err, "WAV sem chunk fmt valido");
    if (channels > 8) return fail(err, "WAV com " + std::to_string(channels) + " canais: sem suporte");
    if (!hasData) return fail(err, "WAV sem chunk data (truncado?)");
    auto s       = std::make_shared<SoundData>();
    s->rate      = rate;
    s->channels  = uint8_t(channels);
    s->loopStart = loopStart, s->loopEnd = loopEnd;
    if (tag == 1 && bits == 16) {
        s->pcm.resize(data.size() / 2);
        std::memcpy(s->pcm.data(), data.data(), s->pcm.size() * 2);
    } else if (tag == 1 && bits == 8) {
        s->pcm.reserve(data.size());
        for (uint8_t v : data) s->pcm.push_back(int16_t((int(v) - 128) << 8));
    } else if (tag == 0 && bits == 4 && channels == 1) {  // ADPCM da AICA (os WAV de eventos do jogo)
        AicaAdpcmState st;
        aica_adpcm_decode(data, s->pcm, st);
    } else {
        return fail(err, "formato de WAV sem suporte (tag " + std::to_string(tag) + ", " + std::to_string(bits) + " bits)");
    }
    s->pcm.resize(s->pcm.size() - s->pcm.size() % channels);  // quadro incompleto no fim: descartado
    return s;
}

std::shared_ptr<SoundData> load_sound(const fs::path& file, std::string* err) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail(err, "nao abre " + file.string());
    in.seekg(0, std::ios::end);
    if (const auto n = in.tellg(); n < 0 || uint64_t(n) > kMaxSoundFileBytes) return fail(err, file.filename().string() + ": grande demais");
    in.seekg(0);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    std::string ext = file.extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    std::string why;
    std::shared_ptr<SoundData> s;
    if (ext == ".flac") s = parse_flac(bytes, &why);
    else if (ext == ".ogg") s = parse_ogg(bytes, &why);
    else s = parse_wav(bytes, &why);
    if (!s) return fail(err, file.filename().string() + ": " + why);
    return s;
}

} // namespace bs::native::audio
