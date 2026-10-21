#include "audio/manatee_seq.h"

#include <algorithm>

namespace bs::native::audio::manatee {

Sequencer::Sequencer(const Sequence& seq, uint32_t firstNoteId) : seq_(seq), nextNote_(firstNoteId) {
    mult_  = int32_t(seq.header1);
    tempo_ = int32_t(seq.header2);
    unit_  = (int64_t(mult_) * tempo_) >> 4;  // 0x74A4
}

uint32_t Sequencer::index_of(uint32_t filePos) const {
    // eventos em ordem de posicao no arquivo
    auto it = std::lower_bound(seq_.events.begin(), seq_.events.end(), filePos,
                               [](const SeqEvent& e, uint32_t p) { return e.pos < p; });
    return it != seq_.events.end() && it->pos == filePos ? uint32_t(it - seq_.events.begin()) : uint32_t(seq_.events.size());
}

void Sequencer::call(std::vector<MusicEvent>& out) {
    ++calls_;
    // notas: o gate cai pelo mesmo passo da trilha (0x4270); acabou -> desliga
    for (auto it = notes_.begin(); it != notes_.end();) {
        it->left -= step_;
        if (it->left <= 0) {
            out.push_back({MusicEvent::Kind::NoteOff, calls_, it->canal, it->tecla, 0, it->id});
            it = notes_.erase(it);
        } else {
            ++it;
        }
    }
    if (ended_) return;
    count_ -= step_;  // 0x4354
    if (count_ > 0) return;
    events(out);
}

void Sequencer::events(std::vector<MusicEvent>& out) {
    using K = SeqEvent::Kind;
    constexpr int kGuard = 1 << 16;  // eventos sem delta seguidos (laco sem fim de referencias ruins)
    for (int guard = 0; guard < kGuard && count_ <= 0; ++guard) {
        // referencia: o contador e conferido antes de cada evento lido depois do primeiro (0x4364)
        if (refLeft_ > 0) {
            if (refLeft_ == 1) idx_ = refReturn_;
            --refLeft_;
        }
        if (idx_ >= seq_.events.size()) {
            ended_ = true;
            out.push_back({MusicEvent::Kind::End, calls_});
            return;
        }
        const SeqEvent& e = seq_.events[idx_++];
        switch (e.kind) {
        case K::Note: {
            const uint32_t id = nextNote_++;
            if (notes_.size() >= kMaxNotes) {
                // lista cheia: o driver marca o estado (0x449C) e reaproveita uma entrada (a de menor prioridade e,
                // entre iguais, a mais antiga; 0x44AC-0x450C): aqui a mais antiga, desligada antes do gate. Nas 11
                // musicas nao acontece (teste audio_manatee_synth_real_songs).
                out.push_back({MusicEvent::Kind::NoteOff, calls_, notes_.front().canal, notes_.front().tecla, 0, notes_.front().id});
                notes_.erase(notes_.begin());
                ++stolen_;
            }
            out.push_back({MusicEvent::Kind::NoteOn, calls_, e.canal, e.a, e.b, id});
            // gate * unidade + a contagem que sobrou (0x4430-0x4444)
            notes_.push_back({id, e.canal, e.a, int64_t(e.gate) * unit_ + count_});
            count_ += int64_t(e.delta) * unit_;  // 0x4874
            break;
        }
        case K::Control: out.push_back({MusicEvent::Kind::Control, calls_, e.canal, e.a, e.b}), count_ += int64_t(e.delta) * unit_; break;
        case K::Program: out.push_back({MusicEvent::Kind::Program, calls_, e.canal, e.a}), count_ += int64_t(e.delta) * unit_; break;
        case K::Pressure: out.push_back({MusicEvent::Kind::Pressure, calls_, e.canal, e.a}), count_ += int64_t(e.delta) * unit_; break;
        case K::Pitch: out.push_back({MusicEvent::Kind::Pitch, calls_, e.canal, e.a}), count_ += int64_t(e.delta) * unit_; break;
        case K::Tempo:  // 0x46CC: tempo e unidade novos, depois o delta
            tempo_ = int32_t(e.ref), unit_ = (int64_t(mult_) * tempo_) >> 4;
            count_ += int64_t(e.delta) * unit_;
            break;
        case K::Reference: {  // 0x4630: o primeiro evento referenciado e lido ja, sem passar pelo contador
            const uint32_t alvo = index_of(seq_.start - 12 + e.ref);  // deslocamento a partir do inicio do SMSD
            if (alvo >= seq_.events.size()) {  // referencia que nao cai num evento: o original leria lixo; aqui acaba
                ended_ = true;
                out.push_back({MusicEvent::Kind::End, calls_});
                return;
            }
            refReturn_ = idx_, idx_ = alvo;
            // o driver le o primeiro evento referenciado direto (0x4650 -> 0x4380, sem o contador) e confere o contador
            // antes das leituras seguintes, voltando quando ele vale 1: N eventos. Aqui o contador e conferido antes de
            // toda leitura, entao comeca em N+1. Contador 0 nunca volta (o driver so volta no 1): segue do alvo.
            refLeft_ = e.count == 0 ? 0 : e.count + 1;
            break;
        }
        case K::Loop:  // 0x4654: o primeiro guarda o ponto; os seguintes voltam ate o contador acabar (0x80 = sem fim)
            if (loopAt_ < 0) {
                loopAt_   = int64_t(idx_ - 1);
                loopLeft_ = e.count == 0 ? 0x80 : e.count;
                count_ += int64_t(e.delta) * unit_;
            } else if (loopLeft_ != 0) {
                ++loops_;
                if (loopLeft_ != 0x80) --loopLeft_;
                const SeqEvent& ini = seq_.events[size_t(loopAt_)];  // relê o delta do primeiro 0x82
                idx_                = uint32_t(loopAt_ + 1);
                count_ += int64_t(ini.delta) * unit_;
            } else {
                loopAt_ = -1;
                count_ += int64_t(e.delta) * unit_;
            }
            break;
        case K::End:
            ended_ = true;
            out.push_back({MusicEvent::Kind::End, calls_});
            return;
        case K::Ignored: break;  // o driver segue para o proximo evento sem delta
        }
    }
}

} // namespace bs::native::audio::manatee
