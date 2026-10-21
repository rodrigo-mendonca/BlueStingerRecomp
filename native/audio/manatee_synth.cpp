#include "audio/manatee_synth.h"

#include "audio/aica_adpcm.h"
#include "audio/sd_adapter.h"

#include <algorithm>
#include <cmath>

namespace bs::native::audio::manatee {

double PitchReg::ratio() const { return std::ldexp(1.0 + fns / 1024.0, oct); }

PitchReg pitch_for(uint8_t key, uint8_t keyBase, int8_t fine, int32_t extra256) {
    PitchReg r;
    const int32_t acc = extra256 + fine;  // 1/256 de semitom
    int32_t       i   = (acc >> 8) + key + 0x60 - (keyBase & 0x7F);
    i &= 0xFF;  // o driver mascara o indice em 8 bits (0x14E0)
    if (i >= 192) i = 191, r.saturated = true;  // alem da oitava +7 a tabela do driver le outros dados
    const int rel = i - 0x60;
    r.oct         = rel >= 0 ? rel / 12 : -((-rel + 11) / 12);
    const int sem = rel - r.oct * 12;
    const int j   = sem * 128 + ((acc & 0xFE) >> 1);
    r.fns         = uint32_t(std::floor(1024.0 * (std::exp2(j / 1536.0) - 1.0)));  // tabela 0x35E8 (iguais as 1536)
    if (r.fns > 1023) r.fns = 1023;
    return r;
}

uint8_t layer_pan_position(uint8_t d) {
    int r = d & 0x1F;
    if (r == 0x10) r = 0;
    if (r & 0x10) r = ((r - 0x10) ^ 0xFF);
    return uint8_t(((r + 0x10) << 2) & 0x7F);
}

uint8_t music_dipan(uint8_t position, int8_t portPan) {
    const int x = (std::clamp(int(position & 0x7F) + portPan, 0, 127) >> 2) - 16;
    return uint8_t(x >= 0 ? x : 0x10 | (~x & 0xF));
}

Synth::Synth(AudioMixer& mixer, const DriverTables& drv, Bus bus) : mixer_(mixer), drv_(drv), bus_(bus) {}

Synth::Synth(AudioMixer& mixer, const Bank& bank, const DriverTables& drv, Bus bus) : Synth(mixer, drv, bus) {
    banks_[0] = &bank;
}

void Synth::set_bank(uint8_t slot, const Bank* bank) {
    if (slot >= kBankSlots) return;
    banks_[slot] = bank;
    ++gen_[slot];
    std::erase_if(cache_, [slot](const auto& kv) { return kv.first.slot == slot; });
}

void Synth::reset_channels() {
    for (auto& c : canais_) {
        const uint8_t program = c.program, bank = c.bank;
        c = Canal{};
        c.program = program, c.bank = bank;  // 0x2758 nao toca no programa nem no banco
    }
    release_pedal(0, true);
}

void Synth::key_off_all() {
    for (auto& v : voices_)
        if (!v.env.releasing()) v.held = false, v.env.key_off();
}

void Synth::release_pedal(uint8_t canal, bool todos) {  // 0x20B4-0x2124 e 0x27FC-0x2860
    for (auto& v : voices_)
        if ((todos || v.canal == canal) && v.held) v.held = false, v.env.key_off();
}

int Synth::channel_level(const Canal& c) const {  // 0x1B5C-0x1B7C: o byte 0x0A do canal
    return ((drv_.volume[c.volume & 0x7F] + 1) * (drv_.volume[c.expression & 0x7F] + 1) >> 7) - 1;
}

uint8_t Synth::dipan_of(const Voice& v) const {
    const Canal& c = canais_[v.canal & 15];
    return music_dipan((c.pan & 0x80) ? v.layerPan : c.pan, portPan_);
}

std::shared_ptr<const SoundData> Synth::sample(uint8_t slot, const Bank& bank_, const Layer& l) {
    const Chave chave{slot, gen_[slot], l.sa, uint8_t(l.flags & 3), l.lsa, l.lea};
    auto        it = cache_.find(chave);
    if (it != cache_.end()) return it->second;
    std::shared_ptr<SoundData> s;
    if ((l.flags & 1) && l.sa < bank_.data.size()) {  // ADPCM da AICA (todas as camadas dos 11 bancos)
        s       = std::make_shared<SoundData>();
        s->rate = 44100;  // a taxa base da AICA: o tom vem do PitchReg
        AicaAdpcmState st;
        const size_t   bytes = (size_t(l.lea) + 1) / 2;
        aica_adpcm_decode(bank_.data.subspan(l.sa, std::min(bytes, bank_.data.size() - l.sa)), s->pcm, st, l.lea);
        if ((l.flags & 2) && l.lsa < l.lea) s->loopStart = l.lsa, s->loopEnd = l.lea;
    } else {
        ++stats_.decodeFailures;  // PCM: nenhum banco do disco usa; sem prova do formato, nao toca
    }
    cache_[chave] = s;
    return s;
}

void Synth::apply(Voice& v) {
    const Canal& c  = canais_[v.canal & 15];
    const int    tl = 255 - std::clamp(v.level + portVolume_, 0, 255);  // 0x1050-0x106C
    mixer_.set_volume(v.id, std::clamp(std::pow(10.0f, -0.375f * tl / 20.0f) * v.sdlGain * v.env.gain(), 0.0f, 1.0f));
    mixer_.set_pan(v.id, dipan_pan(dipan_of(v)));
}

void Synth::on_event(const MusicEvent& e) {
    using K  = MusicEvent::Kind;
    Canal& c = canais_[e.canal & 15];
    switch (e.kind) {
    case K::Program: c.program = e.a; break;
    case K::Control:
        if (e.a == 7 || e.a == 11) {
            (e.a == 7 ? c.volume : c.expression) = e.b;
            c.level = uint8_t(channel_level(c));
            for (auto& v : voices_)  // 0x1BA4-0x1C3C: o nivel das vozes do canal com a nova curva
                if (v.canal == e.canal) v.level = int(((int64_t(v.curva) + 1) * (c.level + 1) * (256 - v.layerTl) - 1) >> 14) & 0xFF;
        } else if (e.a == 10) c.pan = e.b & 0x7F;
        else if (e.a == 32) c.bank = e.b & 0x7F;
        else if (e.a == 64) {
            c.sustain = (e.b & 0x40) != 0;
            if (!c.sustain) release_pedal(e.canal, false);  // pedal solto: as notas que esperavam entram na soltura
        } else if (e.a == 121) {
            reset_channels();  // os 16 canais da porta (0x2758), nao so este
            for (auto& v : voices_) apply(v);
            break;
        }
        for (auto& v : voices_)
            if (v.canal == e.canal) apply(v);
        break;
    case K::NoteOn: {
        ++stats_.notes;
        const Bank* bp = c.bank < kBankSlots ? banks_[c.bank] : nullptr;
        if (!bp || c.program >= bp->programs.size()) {  // slot vazio (0x3DE8) ou programa fora: o driver nao toca
            ++stats_.noLayer;
            break;
        }
        const Bank& bank_ = *bp;
        bool        algum = false;
        for (const Group& g : bank_.programs[c.program].groups)
            for (const Layer& l : g.layers) {
                if (e.a < l.keyMin || e.a > l.keyMax || e.b < l.velMin || e.b > l.velMax) continue;
                auto s = sample(c.bank, bank_, l);
                if (!s || s->pcm.empty()) break;
                const PitchReg p = pitch_for(e.a, l.keyBase, int8_t(l.fine));
                stats_.saturatedPitch += p.saturated;
                PlayParams pp;
                pp.bus   = bus_;
                pp.loop  = s->loopStart >= 0;
                pp.pitch = float(p.ratio());
                pp.tag   = e.noteId;
                const VoiceId id = mixer_.play(s, pp);
                if (!id) {  // sem voz: o driver tenta a proxima camada do grupo (0x0BB4)
                    ++stats_.refused;
                    continue;
                }
                // o nivel do driver (0x0FF4): curva da camada, controle 7 e TL da camada
                const uint8_t ci    = l.raw[0x2A];
                const int     curva = ci < bank_.velocityCurves.size() ? bank_.velocityCurves[ci][e.b & 0x7F] : (e.b & 0x7F);
                const int64_t prod  = int64_t(curva + 1) * (c.level + 1) * (256 - l.tl) - 1;
                const int     level = int((prod >> 14) & 0xFF);
                voices_.push_back({id, e.noteId, e.canal, level});
                voices_.back().env.key_on();
                voices_.back().curva = uint8_t(curva), voices_.back().layerTl = l.tl;
                voices_.back().layerPan = layer_pan_position(l.raw[0x12]);
                const int sdl = std::clamp(int(l.raw[0x13] & 0xF), 0, 15);  // + (porta 0x24 >> 3) - 16 = 0
                voices_.back().sdlGain = sdl == 0 ? 0.0f : std::pow(10.0f, -3.0f * (15 - sdl) / 20.0f);
                apply(voices_.back());
                if (onVoice) {
                    VoiceLog vl;
                    vl.noteId = e.noteId, vl.canal = e.canal, vl.key = e.a, vl.vel = e.b, vl.sa = l.sa, vl.pitch = p;
                    vl.tl   = uint8_t(255 - std::clamp(level + portVolume_, 0, 255));
                    vl.dipan = dipan_of(voices_.back());
                    vl.sdl   = uint8_t(sdl);
                    vl.loop = pp.loop, vl.curva = uint8_t(curva), vl.vol7 = c.volume, vl.layerTl = l.tl, vl.curvaIdx = ci;
                    onVoice(vl);
                }
                ++stats_.voices, algum = true;
                break;  // uma camada por grupo
            }
        if (!algum) ++stats_.noLayer;
        break;
    }
    case K::NoteOff:
        for (auto& v : voices_)
            if (v.noteId == e.noteId && !v.env.releasing()) {
                if (c.sustain) v.held = true;  // o pedal segura
                else v.env.key_off();          // a soltura do envelope compartilhado (fase G: a da AICA)
            }
        break;
    default: break;
    }
}

void Synth::control_tick() {
    for (auto it = voices_.begin(); it != voices_.end();) {
        if (!mixer_.playing(it->id)) {
            it = voices_.erase(it);
            continue;
        }
        if (it->env.tick()) {
            if (it->env.done()) {
                mixer_.stop(it->id);
                it = voices_.erase(it);
                continue;
            }
            apply(*it);
        }
        ++it;
    }
}

void Synth::set_port_pan(int8_t v) {
    portPan_ = v;
    for (auto& x : voices_) apply(x);
}

void Synth::set_port_volume(int v) {
    portVolume_ = v;
    for (auto& v : voices_) apply(v);
}

void Synth::stop_all() {
    for (auto& v : voices_) mixer_.stop(v.id);
    voices_.clear();
}

} // namespace bs::native::audio::manatee
