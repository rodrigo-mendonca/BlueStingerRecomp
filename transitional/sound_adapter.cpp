#include "sound_adapter.h"

#include "audio/audio.h"
#include "audio/mixer.h"
#include "audio/sd_adapter.h"
#include "audio/sosb.h"
#include "audio/sound_catalog.h"
#include "audio/sound_file.h"
#include "audio/voice_player.h"
#include "guest_abi.h"
#include "mods/mod_api.h"
#include "time_adapter.h"
#include "voice_mw_glue.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>

namespace bs::transitional::sound {

namespace na = bs::native::audio;

namespace {

constexpr uint32_t kStop = 0x8C15E208, kPlay = 0x8C15E088, kVolume = 0x8C15E19C, kPan = 0x8C15E11C, kStatus = 0x8C15DF44;
constexpr uint32_t kInitFlag = 0x8C1DDB90;  // o flag que as entradas originais conferem (0 = nao inicializado)

struct State {
    na::AudioMixer                          mixer;
    std::unique_ptr<na::SdAdapter>          sd;
    std::unique_ptr<na::SosbBanks>          banks;
    std::map<uint32_t, std::shared_ptr<const na::SoundData>> cache;
    std::unique_ptr<na::VoicePlayer>        voice;
    std::unique_ptr<VoiceGlue>              glue;
    std::unique_ptr<na::SoundCatalog>       catalog;  // dublagens (mods\audio)
    std::map<std::string, std::shared_ptr<const na::SoundData>> voiceCache;  // originais por nome
    std::filesystem::path                   voiceDir;
    std::string                             language;
    std::unique_ptr<na::IAudioOutput>       out;
    uint32_t                                rate = 44100;
    uint64_t                                rendered = 0;
    uint64_t                                calls[5] = {}, decodeFailures = 0, voiceFailures = 0, dubs = 0;
    std::vector<int16_t>                    buf;
    // Diagnostico BS_EFEITOS_WAV=<arquivo>: a mistura nativa (efeitos e falas) gravada em WAV (PCM 16 estereo), para
    // ouvir o que o headless produz. Desligado por padrao.
    std::FILE*                              wav = nullptr;
    uint64_t                                wavFrames = 0;
    // Diagnostico BS_EFEITOS_LOG=<arquivo>: as chamadas do jogo, os fins de canal e os eventos das falas por passo de
    // jogo, no vocabulario do oraculo O6 (tools/dev/n6_oraculo.py) para a comparacao com o Flycast.
    std::FILE*                              log = nullptr;
    uint64_t                                vb = 0;
    uint32_t                                lastWord[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    bool                                    busy[8] = {};
    uint64_t                                lastStep = 0;  // passos de jogo ja entregues ao VoicePlayer
    std::function<void(const na::DialogueEvent&)> onDialogue;
    explicit State(uint32_t r) : mixer(r, 64), rate(r) {}
};

void wavHeader(std::FILE* f, uint32_t rate, uint64_t frames) {
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t data = uint32_t(frames * 4);
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f), u32(36 + data), std::fwrite("WAVEfmt ", 1, 8, f), u32(16), u16(1), u16(2), u32(rate),
        u32(rate * 4), u16(4), u16(16), std::fwrite("data", 1, 4, f), u32(data);
}
std::unique_ptr<State> g;
bool                   g_installed = false, g_effects = false, g_voices = false;

// porta do driver = byte 1 do handle (o mesmo que o original manda no pacote); handle nulo = nullptr
std::optional<na::SdHandle> handle(GuestCall& call, uint32_t h) {
    if (h == 0) return std::nullopt;
    const auto port = call.memory().load<uint8_t>(h + 1);
    return na::SdHandle{port ? *port : uint8_t(0)};
}
int8_t s8(uint32_t v) { return int8_t(uint8_t(v)); }

template <class... A>
void logf(const char* tipo, const char* fmt, A... a) {
    if (!g->log) return;
    std::fprintf(g->log, "%llu\t%llu\t%s\t", static_cast<unsigned long long>(time::game_steps()),
                 static_cast<unsigned long long>(g->vb), tipo);
    std::fprintf(g->log, fmt, a...);
    std::fputc('\n', g->log);
}

template <uint32_t Entry>
void slot(Sh4Context* c) {
    GuestCall  call(c);
    const auto init = call.memory().load<uint32_t>(kInitFlag);
    g->sd->set_initialized(init && *init != 0);
    const auto      h  = handle(call, c->r[4]);
    const na::SdHandle* hp = h ? &*h : nullptr;
    uint32_t        r0 = 0;
    const int       pt = h ? int(h->port) : -1;
    if constexpr (Entry == kStop) r0 = g->sd->stop(hp), ++g->calls[0], logf("parar", "porta=%d", pt);
    if constexpr (Entry == kPlay) {
        r0 = g->sd->play(hp, s8(c->r[5]), s8(c->r[6]), s8(c->r[7])), ++g->calls[1];
        logf("tocar", "porta=%d banco=%d pedido=%d x=%d r0=%u", pt, s8(c->r[5]), s8(c->r[6]), s8(c->r[7]), r0);
        if (pt >= 0 && pt < 8) g->busy[pt] = g->sd->busy(uint8_t(pt));
    }
    if constexpr (Entry == kVolume) r0 = g->sd->param15(hp, s8(c->r[5]), uint16_t(c->r[6])), ++g->calls[2], logf("p15", "porta=%d v=%d t=%u", pt, s8(c->r[5]), c->r[6] & 0xFFFF);
    if constexpr (Entry == kPan) r0 = g->sd->param16(hp, s8(c->r[5]), uint16_t(c->r[6])), ++g->calls[3], logf("p16", "porta=%d v=%d t=%u", pt, s8(c->r[5]), c->r[6] & 0xFFFF);
    if constexpr (Entry == kStatus) {
        ++g->calls[4];
        uint8_t st[28];
        r0 = g->sd->status(hp, c->r[5] ? st : nullptr);
        if (r0 == 0 && c->r[5]) {  // so o que o original escreve: os 28 bytes da saida
            for (uint32_t i = 0; i < 28; i += 4) {
                uint32_t w;
                std::memcpy(&w, st + i, 4);
                call.memory().store<uint32_t>(c->r[5] + i, w);
            }
            uint32_t w24;
            std::memcpy(&w24, st + 24, 4);
            if (pt >= 0 && pt < 8 && g->lastWord[pt] != w24) g->lastWord[pt] = w24, logf("estado", "porta=%d +24=%08X", pt, w24);
        } else if (r0)
            logf("estado", "porta=%d erro=%08X", pt, r0);
    }
    call.ret(r0);
}

const std::pair<uint32_t, Sh4Func> kFns[] = {
    {kStop, &slot<kStop>}, {kPlay, &slot<kPlay>}, {kVolume, &slot<kVolume>}, {kPan, &slot<kPan>}, {kStatus, &slot<kStatus>}};

// Falas: a memoria do guest para a cola (voice_mw_glue.h)
class SlotGuest final : public VoiceGuest {
public:
    explicit SlotGuest(Sh4Context* c) : call_(c) {}
    std::optional<uint32_t>    load32(uint32_t a) override { return call_.memory().load<uint32_t>(a); }
    bool                       store32(uint32_t a, uint32_t v) override { return call_.memory().store<uint32_t>(a, v); }
    std::optional<std::string> cstring(uint32_t a, size_t max) override { return call_.memory().cstring(a, max); }
    GuestCall&                 call() { return call_; }

private:
    GuestCall call_;
};

const char* statName(int32_t s) {
    static const char* const k[] = {"parado", "preparando", "tocando", "fim", "erro"};
    return s >= 0 && s <= 4 ? k[s] : "?";
}

template <uint32_t Entry>
void wavSlot(Sh4Context* c) {
    SlotGuest          port(c);
    const uint32_t     r4 = c->r[4], r5 = c->r[5];
    const int32_t      before = g->voice->state();
    const VoiceOutcome r      = g->glue->call(Entry, r4, r5, port);
    if (r.forward) return mods::call_original(Entry, c);
    if constexpr (Entry == kWavStart) logf("fala-start", "nome=%s", g->voice->current().c_str());
    if constexpr (Entry == kWavStat) {
        if (int32_t(r.r0) != before || r.r0 >= 3) logf("fala-estado", "%s (%d)", statName(int32_t(r.r0)), int32_t(r.r0));
    }
    if constexpr (Entry == kWavStop) logf("fala-stop", "estado=%s", statName(before));
    port.call().ret(r.r0);
}

const std::pair<uint32_t, Sh4Func> kWavFns[] = {
    {kWavCreate, &wavSlot<kWavCreate>}, {kWavServer0C, &wavSlot<kWavServer0C>}, {kWavServer10, &wavSlot<kWavServer10>},
    {kWavDestroy, &wavSlot<kWavDestroy>}, {kWavStart, &wavSlot<kWavStart>},       {kWavStop, &wavSlot<kWavStop>},
    {kWavStat, &wavSlot<kWavStat>},     {kWav24, &wavSlot<kWav24>},             {kWavMode, &wavSlot<kWavMode>},
    {kWav2C, &wavSlot<kWav2C>},         {kWav30, &wavSlot<kWav30>},             {kWav34, &wavSlot<kWav34>}};

const char* dialogueName(na::DialogueKind k) {
    switch (k) {
    case na::DialogueKind::Started: return "inicio";
    case na::DialogueKind::LogicalEnd: return "fim-logico";
    case na::DialogueKind::PhysicalEnd: return "fim-fisico";
    case na::DialogueKind::Interrupted: return "interrompida";
    case na::DialogueKind::Missing: return "ausente";
    case na::DialogueKind::Paused: return "pausa";
    case na::DialogueKind::Resumed: return "retomada";
    }
    return "?";
}

// "E001A.WAV" -> "voz/e001a" (o ID estavel em minusculas do docs/LOCALIZACAO-PLANO.md); "" se o nome nao for um WAV
std::string voiceId(std::string_view name) {
    std::string s(name);
    if (s.size() < 5) return {};
    std::string ext = s.substr(s.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
    if (ext != ".WAV") return {};
    s.resize(s.size() - 4);
    for (char& ch : s) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') return {};
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    }
    return "voz/" + s;
}

// A fala pelo nome que o jogo passa: a dublagem do idioma (mods\audio) ou o WAV original do disco. O fim que o jogo ve
// e sempre o da fala original: uma dublagem mais longa avisa o fim logico ali e continua (TailPolicy); uma mais curta
// e completada com silencio ate la (o jogo nao pode ver a fala acabar antes).
na::VoiceSource resolveVoice(State& st, std::string_view name) {
    na::VoiceSource v;
    v.id = voiceId(name);
    if (v.id.empty()) return v;
    auto it = st.voiceCache.find(v.id);
    if (it == st.voiceCache.end()) {
        std::string err;
        auto        s = na::load_sound(st.voiceDir / std::filesystem::path(std::string(name)), &err);
        if (!s) ++st.voiceFailures;
        it = st.voiceCache.emplace(v.id, std::move(s)).first;
    }
    const auto& orig = it->second;
    v.data           = orig;
    if (!st.catalog || st.language.empty()) return v;
    const auto e = st.catalog->resolve(v.id, st.language);
    if (!e || !e->mod) return v;
    std::string err;
    auto        dub = na::load_sound(e->file, &err);
    if (!dub || !dub->rate || !orig || !orig->rate) {
        ++st.voiceFailures;
        return v;  // dublagem ilegivel: o original (e o problema contado)
    }
    na::SoundEntry ent    = *e;
    ent.originalSeconds   = double(orig->frames()) / double(orig->rate);
    const int64_t logical = na::logical_end_frames(ent, dub->frames(), dub->rate, dub->rate);
    if (logical > dub->frames()) {  // mais curta: silencio ate o fim original
        auto padded = std::make_shared<na::SoundData>(*dub);
        padded->pcm.resize(size_t(logical) * std::max<size_t>(1, padded->channels), 0);
        dub = std::move(padded);
    }
    v.data       = std::move(dub);
    v.logicalEnd = logical < v.data->frames() ? logical : -1;
    v.mod        = true;
    v.lang       = e->lang;
    ++st.dubs;
    return v;
}

void checkSlots(const std::pair<uint32_t, Sh4Func>* fns, size_t n, const char* what) {
    for (size_t i = 0; i < n; ++i)
        if (!sh4_lookup_generated(fns[i].first) || !mods::has_slot(fns[i].first)) {
            char hexa[16];
            std::snprintf(hexa, sizeof hexa, "%08X", fns[i].first);
            throw std::runtime_error(std::string(what) + ": a entrada " + hexa + " nao tem slot (gere o codigo de novo com [native] slots)");
        }
}

} // namespace

void install(BoundaryRegistry& registry, Options o) {
    if (!o.effects && !o.voices) return;
    if (o.effects) checkSlots(kFns, std::size(kFns), "efeitos nativos");
    if (o.voices) checkSlots(kWavFns, std::size(kWavFns), "falas nativas");
    g = std::make_unique<State>(o.rate);
    if (const char* w = std::getenv("BS_EFEITOS_WAV"); w && *w) {
        g->wav = std::fopen(w, "wb");
        if (g->wav) wavHeader(g->wav, g->rate, 0), std::fseek(g->wav, 44, SEEK_SET);
    }
    if (const char* l = std::getenv("BS_EFEITOS_LOG"); l && *l)
        if ((g->log = std::fopen(l, "w"))) std::fputs("passo\tvblank\ttipo\tdetalhe\n", g->log);
    g->out = o.makeOutput ? o.makeOutput() : na::make_null_output();
    if (!g->out || !g->out->open(g->rate, 2)) g->out = na::make_null_output(), (void)g->out->open(g->rate, 2);
    State* st = g.get();
    // o SdAdapter e o dono dos eventos do mixer; os que nao sao dos canais de efeito (as falas) chegam aqui
    na::SdAdapterOptions sdo;
    sdo.onOtherEvent = [st](const na::MixerEvent& e) {
        if (st->voice) st->voice->on_mixer_event(e);
    };
    if (o.effects) {
        std::ifstream        f(o.mlt, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
        std::string          err;
        auto                 banks = na::SosbBanks::parse(std::move(bytes), &err);
        if (!banks) throw std::runtime_error("efeitos nativos: " + o.mlt.string() + ": " + err);
        g->banks = std::make_unique<na::SosbBanks>(std::move(*banks));
    }
    g->sd = std::make_unique<na::SdAdapter>(
        g->mixer,
        [st](uint8_t bank, uint8_t req) -> na::SdSound {
            if (!st->banks) return {};
            const uint32_t id = uint32_t(bank) * 128 + req;
            auto           it = st->cache.find(id);
            if (it == st->cache.end()) {
                auto s = st->banks->decode(bank, req);
                if (!s) ++st->decodeFailures;
                it = st->cache.emplace(id, std::move(s)).first;
            }
            const na::SosbEffect* e = st->banks->find(bank, req);
            return {it->second, -1, e ? e->baseTl : na::kSdDefaultBaseTl};
        },
        std::move(sdo));
    if (o.voices) {
        g->voiceDir   = o.voiceDir;
        g->language   = o.language;
        g->onDialogue = std::move(o.onDialogue);
        if (!o.modsAudio.empty()) g->catalog = std::make_unique<na::SoundCatalog>(na::SoundCatalog::load({{}, o.modsAudio}));
        na::VoicePlayerOptions vo;
        vo.tail    = o.cutLongerDubs ? na::TailPolicy::Cut : na::TailPolicy::PlayOut;
        vo.onEvent = [st](const na::DialogueEvent& e) {
            logf("fala", "%s %s", dialogueName(e.kind), e.id.c_str());
            if (st->onDialogue) st->onDialogue(e);
        };
        g->voice = std::make_unique<na::VoicePlayer>(g->mixer, [st](std::string_view n) { return resolveVoice(*st, n); }, std::move(vo));
        g->glue  = std::make_unique<VoiceGlue>(*g->voice);
    }
    if (o.effects)
        for (const auto& [a, fn] : kFns) {
            registry.add(a, dcmap::Subsystem::Audio, "sd");
            registry.set_native(a, fn);
            if (registry.select(a, Provider::Native) != SelectResult::Ok) throw std::runtime_error("efeitos nativos: troca de provedor falhou");
        }
    if (o.voices)
        for (const auto& [a, fn] : kWavFns) {
            registry.add(a, dcmap::Subsystem::Audio, "mw-wav");
            registry.set_native(a, fn);
            if (registry.select(a, Provider::Native) != SelectResult::Ok) throw std::runtime_error("falas nativas: troca de provedor falhou");
        }
    g_effects = o.effects, g_voices = o.voices;
    g_installed = true;
}

void uninstall(BoundaryRegistry& registry) {
    if (!g_installed) return;
    if (g_effects)
        for (const auto& [a, fn] : kFns) registry.select(a, Provider::Legacy);
    if (g_voices)
        for (const auto& [a, fn] : kWavFns) registry.select(a, Provider::Legacy);
    if (g->out) g->out->close();
    if (g->wav) wavHeader(g->wav, g->rate, g->wavFrames), std::fclose(g->wav);
    if (g->log) std::fclose(g->log);
    g.reset();
    g_installed = g_effects = g_voices = false;
}

bool installed() { return g_installed; }

void pause(bool on) {
    if (g_installed && g->voice) g->voice->pause(on);
}

void scene_changed() {
    if (g_installed && g->voice) g->voice->scene_changed();
}

void vblank(uint64_t n) {
    if (!g_installed) return;
    g->vb = n;
    // tempo logico: n VBlanks de 1/59,94 s (NTSC 60000/1001)
    const uint64_t upTo = n * uint64_t(g->rate) * 1001 / 60000;
    if (upTo > g->rendered) {
        g->buf.assign(size_t(upTo - g->rendered) * 2, 0);
        g->mixer.render(g->buf);
        g->rendered = upTo;
        (void)g->out->write(g->buf);
        if (g->wav) std::fwrite(g->buf.data(), 2, g->buf.size(), g->wav), g->wavFrames += g->buf.size() / 2;
    }
    // Os passos de jogo concluidos ANTES dos eventos deste trecho: o preparo e o atraso do fim (endSteps) contam so os
    // passos depois do fato (um fim que cai no mesmo VBlank de um passo nao gasta esse passo)
    if (g->voice) {  // o preparo e o fim da fala andam por passo de jogo (o servidor do MW original roda por passo)
        const uint64_t steps = time::game_steps();
        for (; g->lastStep < steps; ++g->lastStep) g->voice->game_step();
    }
    g->sd->sync(upTo);  // a consulta do jogo ve so os fins ate aqui; os eventos das falas seguem para o VoicePlayer
    if (g->log)  // fim do canal (o key-off que o oraculo ve na AICA)
        for (uint8_t p = 0; p < 8; ++p)
            if (g->busy[p] && !g->sd->busy(p)) g->busy[p] = false, logf("fim", "porta=%u id=%u", p, g->sd->tag(p));
}

std::string summary() {
    if (!g) return {};
    std::string r;
    char        b[400];
    if (g_effects) {
        std::snprintf(b, sizeof b,
                      "[audio] efeitos nativos: parar %llu, tocar %llu, volume %llu, pan %llu, consultas %llu; efeitos ausentes %u, "
                      "vozes recusadas %u, falhas de decodificacao %llu\n",
                      static_cast<unsigned long long>(g->calls[0]), static_cast<unsigned long long>(g->calls[1]),
                      static_cast<unsigned long long>(g->calls[2]), static_cast<unsigned long long>(g->calls[3]),
                      static_cast<unsigned long long>(g->calls[4]), g->sd->missing(), g->sd->refused(),
                      static_cast<unsigned long long>(g->decodeFailures));
        r += b;
    }
    if (g_voices) {
        const auto& v = g->voice->stats();
        const auto& s = g->glue->stats();
        // "inesperadas": servidor por handle e metodos sem uso chamados com o handle nativo; tem que ser 0
        std::snprintf(b, sizeof b,
                      "[audio] falas nativas: create %llu (falhas %llu), start %llu, consultas %llu, fins %llu, interrompidas %llu, "
                      "ausentes %llu, dublagens %llu, caudas cortadas %llu, falhas de leitura %llu, outros handles %llu, inesperadas %llu\n",
                      static_cast<unsigned long long>(s.creates), static_cast<unsigned long long>(s.createFailures),
                      static_cast<unsigned long long>(v.starts), static_cast<unsigned long long>(v.statusCalls),
                      static_cast<unsigned long long>(v.logicalEnds), static_cast<unsigned long long>(v.interrupted),
                      static_cast<unsigned long long>(v.missing), static_cast<unsigned long long>(g->dubs),
                      static_cast<unsigned long long>(v.tailsCut), static_cast<unsigned long long>(g->voiceFailures),
                      static_cast<unsigned long long>(s.foreign), static_cast<unsigned long long>(s.unexpected));
        r += b;
    }
    return r;
}

} // namespace bs::transitional::sound
