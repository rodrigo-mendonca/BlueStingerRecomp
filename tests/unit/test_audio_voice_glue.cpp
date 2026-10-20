// N6 fase E: a cola das falas com o guest (transitional/voice_mw_glue) sobre uma RAM sintetica: o pool do MW WAV, o
// handle nativo, os metodos da tabela e o encaminhamento de outros handles ao original.
#include "test.h"

#include "audio/mixer.h"
#include "audio/voice_player.h"
#include "voice_mw_glue.h"

#include <map>
#include <memory>
#include <string>

namespace na = bs::native::audio;
namespace ts = bs::transitional::sound;

namespace {
struct FakeGuest final : ts::VoiceGuest {
    std::map<uint32_t, uint32_t>    mem;
    std::map<uint32_t, std::string> str;
    std::optional<uint32_t>         load32(uint32_t a) override {
        auto it = mem.find(a);
        return it == mem.end() ? 0u : it->second;
    }
    bool store32(uint32_t a, uint32_t v) override { return mem[a] = v, true; }
    std::optional<std::string> cstring(uint32_t a, size_t) override {
        auto it = str.find(a);
        return it == str.end() ? std::optional<std::string>() : it->second;
    }
};
} // namespace

BS_TEST(audio_voice_glue_contract) {
    na::AudioMixer  mixer(44100, 8);
    na::VoicePlayer vp(mixer, [](std::string_view n) {
        na::VoiceSource s;
        if (n != "E012AENV.WAV") return s;
        auto d  = std::make_shared<na::SoundData>();
        d->rate = 44100;
        d->pcm.assign(4410, 1);
        s.data = d, s.id = "voz/e012aenv";
        return s;
    }, [] {
        na::VoicePlayerOptions o;
        o.prepSteps = 0;  // a cola: o estado vem direto do player
        return o;
    }());
    ts::VoiceGlue g(vp);
    FakeGuest     m;
    m.mem[ts::kWavPool + 4] = 1;  // a entrada 0 ja e de um handle do original
    auto c = g.call(ts::kWavCreate, 0x8C0B0000, 0, m);
    const uint32_t h = ts::kWavPool + ts::kWavEntrySize;  // a primeira livre
    BS_CHECK(!c.forward && c.r0 == h && m.mem[h] == ts::kWavVtable && m.mem[h + 4] == 1 && g.handle() == h);
    BS_CHECK(g.call(ts::kWavCreate, 0, 0, m).r0 == 0 && g.stats().createFailures == 1);  // um por vez
    BS_CHECK(g.call(ts::kWavStat, ts::kWavPool, 0, m).forward);  // o handle do original segue para o original
    m.str[0x8C0B8EA0] = "E012AENV.WAV";
    BS_CHECK(!g.call(ts::kWavStart, h, 0x8C0B8EA0, m).forward && vp.current() == "voz/e012aenv");
    BS_CHECK(g.call(ts::kWavStat, h, 0, m).r0 == na::kVoicePlaying);
    BS_CHECK(g.call(ts::kWavServer0C, h, 0, m).r0 == 0 && g.stats().unexpected == 1);  // contado: tem que ficar 0 no jogo
    BS_CHECK(g.call(ts::kWavMode, h, 1, m).r0 == 0 && g.stats().modeCalls == 1);
    g.call(ts::kWavStop, h, 0, m);
    g.call(ts::kWavDestroy, h, 0, m);
    BS_CHECK(g.handle() == 0 && m.mem[h + 4] == 0 && m.mem[h] == 0 && g.stats().destroys == 1);
    BS_CHECK(g.call(ts::kWavStat, h, 0, m).forward);  // depois do destroy, o endereco nao e mais nativo
    // pool cheio: 0, como o original
    for (uint32_t i = 0; i < ts::kWavEntries; ++i) m.mem[ts::kWavPool + i * ts::kWavEntrySize + 4] = 1;
    BS_CHECK(g.call(ts::kWavCreate, 0, 0, m).r0 == 0 && g.stats().createFailures == 2);
    // fala ausente: estado de erro (4)
    m.mem[h + 4] = 0;
    BS_CHECK(g.call(ts::kWavCreate, 0, 0, m).r0 == h);
    m.str[0x8C0B8EB0] = "E999.WAV";
    g.call(ts::kWavStart, h, 0x8C0B8EB0, m);
    BS_CHECK(g.call(ts::kWavStat, h, 0, m).r0 == na::kVoiceError);
}
