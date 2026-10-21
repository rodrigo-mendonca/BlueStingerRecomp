#include "audio/manatee_player.h"

#include <cstring>

namespace bs::native::audio::manatee {

void PortFader::command(uint8_t value, uint16_t time) {
    target = value;
    if (time == 0) {  // 0x4DB4: periodo 0, o valor vai ao alvo no proximo tick
        period = countdown = 0;
        return;
    }
    uint32_t p = time >> 3;
    uint32_t d = uint32_t(current > value ? current - value : value - current) & 0xFF;
    while ((d & 0xFE) && (p & ~1u)) d >>= 1, p >>= 1;  // 0x4DD8-0x4DF0
    period = countdown = uint8_t(p);                    // strb: 8 bits
    step = uint8_t(d);
}

bool PortFader::tick() {
    if (target == current) return false;
    if (countdown) {
        --countdown;
        return false;
    }
    countdown = period;
    if (period == 0) {
        current = target;
    } else if (current < target) {
        const int v = current + step;
        current     = uint8_t(v >= target ? target : v);
    } else {
        const int v = int(current) - step;
        current     = uint8_t(v <= target ? target : v);
    }
    return true;
}

MusicPlayer::MusicPlayer(AudioMixer& mixer, DriverTables drv, Bus bus) : mixer_(mixer), drv_(drv), bus_(bus) {
    for (uint8_t p = 0; p < kPorts; ++p) {
        ports_[p].synth = std::make_unique<Synth>(mixer_, drv_, bus_);
        ports_[p].synth->set_pool(pool_.get());
        pool_->members.push_back(ports_[p].synth.get());
        ports_[p].synth->onVoice = [this, p](const VoiceLog& v) {
            if (onVoice) onVoice(p, v);
        };
    }
}

bool MusicPlayer::download(uint32_t magic, uint8_t slot, std::vector<uint8_t> bytes, std::string* err) {
    if (magic != kMagicSmsb && magic != kMagicSmpb) return false;
    ++stats_.downloads;
    slot &= 0x0F;
    if (magic == kMagicSmsb) {
        auto s   = std::make_shared<SmsbSlot>();
        s->bytes = std::move(bytes);
        auto seqs = parse_smsb(s->bytes, err);
        if (!seqs) {
            ++stats_.refusedDownloads;
            smsb_[slot].reset();
            return true;
        }
        s->seqs     = std::move(*seqs);
        smsb_[slot] = std::move(s);  // as portas que tocam o slot antigo seguem com ele (Port::data)
    } else {
        auto s   = std::make_shared<SmpbSlot>();
        s->bytes = std::move(bytes);
        auto bank = parse_smpb(s->bytes, err);
        if (!bank) ++stats_.refusedDownloads;
        else s->bank = std::move(*bank);
        const Bank* novo = bank ? &s->bank : nullptr;
        for (auto& p : ports_) p.synth->set_bank(slot, novo);  // antes de soltar o antigo
        smpb_[slot] = bank ? std::shared_ptr<const SmpbSlot>(std::move(s)) : nullptr;
    }
    return true;
}

PlayError MusicPlayer::play(uint8_t port, uint8_t smsbSlot, uint8_t sequence, uint8_t /*x*/) {
    ++stats_.plays;
    Port& p = ports_[port % kPorts];
    const auto& d = smsb_[smsbSlot & 0x0F];
    // parse_smsb ja conferiu 'SMSB', a versao e o rodape (0x4B5C-0x4B98): slot vazio ou recusado = erro 2
    if (!d) return ++stats_.playErrors, PlayError::NotSmsb;
    if (sequence >= d->seqs.size()) return ++stats_.playErrors, PlayError::NoSequence;
    if (p.seq) p.nextNoteId = p.seq->next_note_id();
    p.seq.reset();  // o sequenciador antigo referencia a sequencia antiga: sai antes dela
    p.data = d;
    p.seq  = std::make_unique<Sequencer>(p.data->seqs[sequence], p.nextNoteId);
    p.smsbSlot = smsbSlot & 0x0F, p.sequence = sequence, p.calls = 0;
    p.synth->reset_channels();  // 0x7404 -> 0x2868 -> 0x2758
    return PlayError::None;
}

void MusicPlayer::stop(uint8_t port) {
    ++stats_.stops;
    Port& p = ports_[port % kPorts];
    p.synth->key_off_all();  // 0x4C30-0x4C64: KYONB = 0 em todas as vozes da porta (a soltura e do envelope)
    if (p.seq) p.nextNoteId = p.seq->next_note_id();
    p.seq.reset();           // 0x74EC: as trilhas
    p.data.reset();
}

void MusicPlayer::volume(uint8_t port, int8_t v, uint16_t time) {
    ++stats_.volumes;
    ports_[port % kPorts].vol.command(uint8_t(v + 0x80), time);
}

void MusicPlayer::pan(uint8_t port, int8_t v, uint16_t time) {
    ++stats_.pans;
    ports_[port % kPorts].pan.command(uint8_t(v + 0x80), time);
}

void MusicPlayer::tick() {
    ++stats_.ticks;
    for (uint8_t i = 0; i < kPorts; ++i) {
        Port& p = ports_[i];
        if (p.seq) {  // 0x4210
            evbuf_.clear();
            p.seq->call(evbuf_);
            ++p.calls;
            for (const auto& e : evbuf_) {
                p.synth->on_event(e);
                if (onEvent) onEvent(i, e);
            }
        }
        p.synth->control_tick();
        if (p.vol.tick()) p.synth->set_port_volume(int(p.vol.current) - 0x80);  // 0x6640
        if (p.pan.tick()) p.synth->set_port_pan(int8_t(int(p.pan.current) - 0x80));  // 0x66E0
    }
}

PortView MusicPlayer::view(uint8_t port) const {
    const Port& p = ports_[port % kPorts];
    PortView    v;
    v.playing  = p.seq && !p.seq->ended();
    v.smsbSlot = p.smsbSlot, v.sequence = p.sequence, v.volume = p.vol.current, v.pan = p.pan.current;
    v.calls    = p.calls, v.sounding = p.synth->sounding();
    return v;
}

SynthStats MusicPlayer::synth_stats() const {
    SynthStats s;
    for (const auto& p : ports_) {
        const auto& x = p.synth->stats();
        s.notes += x.notes, s.voices += x.voices, s.refused += x.refused, s.noLayer += x.noLayer, s.stolen += x.stolen;
        s.stolenKeyed += x.stolenKeyed, s.stolenLoud += x.stolenLoud;
        s.saturatedPitch += x.saturatedPitch, s.decodeFailures += x.decodeFailures;
    }
    return s;
}

} // namespace bs::native::audio::manatee
