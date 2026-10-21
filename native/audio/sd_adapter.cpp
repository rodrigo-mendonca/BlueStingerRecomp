#include "audio/sd_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace bs::native::audio {

float sd_volume_gain(int8_t v, uint8_t base) { return float(std::pow(10.0, -0.375 * sd_tl(v, base) / 20.0)); }

float sd_pan(int8_t v) { return dipan_pan(sd_dipan(v)); }

float dipan_pan(uint8_t d) {
    const int     level = d & 0xF;
    const double  other = level == 15 ? 0.0 : std::pow(10.0, -3.0 * level / 20.0);
    // mixer: o lado oposto ao pan recebe 1 - |pan| (mixer.cpp); 0x10 = som a esquerda (o direito atenuado)
    return float((d & 0x10) ? -(1.0 - other) : (1.0 - other));
}

SdAdapter::SdAdapter(AudioMixer& mixer, SdResolver resolver, SdAdapterOptions o)
    : mixer_(mixer), resolver_(std::move(resolver)), o_(std::move(o)) {}

void SdAdapter::release(Port& p) {
    p.voice = 0;
    p.busy  = false;
}

void SdAdapter::drop(Port& p) {
    for (VoiceId* v : {&p.voice, &p.tail}) {
        if (*v) {
            mixer_.stop(*v);
            dropped_.push_back(*v);
        }
    }
    p.tail = 0;
    release(p);
}

uint32_t SdAdapter::stop(const SdHandle* h) {
    if (!init_) return sd_err::kNotInitialized;
    if (!h) return sd_err::kNullHandle;
    drop(ports_[h->port]);  // o evento Stopped dessas vozes e descartado no sync (porta ja liberada)
    return sd_err::kOk;
}

uint32_t SdAdapter::play(const SdHandle* h, int8_t bank, int8_t request, int8_t x) {
    if (!init_) return sd_err::kNotInitialized;
    if (!h) return sd_err::kNullHandle;
    if (x < 0 || x > 15) return sd_err::kRange;
    Port& p = ports_[h->port];
    drop(p);  // tocar no mesmo canal recomeca (key-on), inclusive o resto de um mod mais longo
    p.tag = uint32_t(uint8_t(bank)) * 128 + uint8_t(request);
    SdSound s = resolver_ ? resolver_(uint8_t(bank), uint8_t(request)) : SdSound{};
    if (!s.data) {
        ++missing_;
        return sd_err::kOk;  // o jogo ignora o retorno; o canal fica livre (a consulta diz "acabou")
    }
    PlayParams pp;
    pp.bus        = o_.bus;
    pp.tag        = p.tag;
    pp.logicalEnd = s.logicalEnd;
    // laco da AICA (LPCTL): o som com pontos de laco toca LSA..LEA ate o parar; o canal continua ocupado (O6
    // 2026-10-20: +24 = 1 na consulta enquanto um efeito com laco soava)
    pp.loop       = s.data->loopStart >= 0 && s.data->loopEnd > s.data->loopStart;
    p.baseTl      = s.baseTl;
    p.voice       = mixer_.play(std::move(s.data), pp);
    if (!p.voice) {
        ++refused_;
        return sd_err::kOk;
    }
    p.busy = true;
    apply(p);
    return sd_err::kOk;
}

void SdAdapter::apply(Port& p) {
    if (!p.voice) return;  // a cauda de um mod mais longo nao muda: o jogo ja considera o canal livre
    float pan = 0.0f, vol = 1.0f;
    const std::pair<SdParamRole, int8_t> params[] = {{o_.cmd15, p.p15}, {o_.cmd16, p.p16}};
    for (const auto& [role, v] : params) {
        if (role == SdParamRole::Pan) pan = o_.toPan(v);
        if (role == SdParamRole::Volume) vol = o_.toVolume(v, p.baseTl);
    }
    mixer_.set_pan(p.voice, pan);
    mixer_.set_volume(p.voice, vol);
}

uint32_t SdAdapter::param(const SdHandle* h, int8_t value, uint16_t time, bool cmd15, bool checkInit) {
    if (checkInit && !init_) return sd_err::kNotInitialized;
    if (!h) return sd_err::kNullHandle;
    if (time != 0) return sd_err::kUnsupported;
    Port& p = ports_[h->port];
    (cmd15 ? p.p15 : p.p16) = value;  // vale para a voz atual e as proximas do canal
    apply(p);
    return sd_err::kOk;
}

uint32_t SdAdapter::param15(const SdHandle* h, int8_t value, uint16_t time) { return param(h, value, time, true, false); }
uint32_t SdAdapter::param16(const SdHandle* h, int8_t value, uint16_t time) { return param(h, value, time, false, true); }

uint32_t SdAdapter::status(const SdHandle* h, uint8_t* out28) {
    if (!init_) return sd_err::kNotInitialized;
    if (!h) return sd_err::kNullHandle;
    if (!out28) return sd_err::kNullOut;
    std::memset(out28, 0, 28);  // +0/+1 zerados pelo original; +2..+27 do driver: so +24 tem significado aqui
    const uint32_t playing = ports_[h->port].busy ? 1u : 0u;
    std::memcpy(out28 + 24, &playing, 4);  // palavra de 32 bits little-endian (mov.l @(24,r15) no jogo)
    return sd_err::kOk;
}

void SdAdapter::other(const MixerEvent& e) {
    if (o_.onOtherEvent) {
        o_.onOtherEvent(e);
    } else if (others_.size() < o_.otherEventsCap) {
        others_.push_back(e);
    } else {
        ++overflow_;
    }
}

void SdAdapter::sync(uint64_t upTo) {
    for (const MixerEvent& e : mixer_.take_events(upTo)) {
        const bool ends = e.kind == EventKind::Finished || e.kind == EventKind::Stopped || e.kind == EventKind::Stolen;
        bool       mine = false;
        for (Port& p : ports_) {
            if (p.voice && p.voice == e.voice) {
                mine = true;
                if (e.kind == EventKind::LogicalEnd) {  // o jogo ve o fim aqui; o som segue como cauda
                    p.tail = p.voice;
                    release(p);
                } else if (ends) {
                    release(p);
                }
                break;
            }
            if (p.tail && p.tail == e.voice) {
                mine = true;
                if (ends) p.tail = 0;
                break;
            }
        }
        if (mine) continue;
        // uma voz que o adaptador ja soltou (parada ou recomecada) nao e de mais ninguem: o evento morre aqui
        auto d = std::find(dropped_.begin(), dropped_.end(), e.voice);
        if (d == dropped_.end()) {
            other(e);
        } else if (ends) {
            dropped_.erase(d);
        }
    }
}

std::vector<MixerEvent> SdAdapter::take_other_events() { return std::exchange(others_, {}); }

} // namespace bs::native::audio
