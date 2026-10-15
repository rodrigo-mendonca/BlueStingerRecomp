#pragma once
// Tabela de handles de 32 bits com geracao (modelo de interoperabilidade da fase hibrida,
// docs/PLANO-PORT-NATIVO.md secao 2). Objetos nativos ficam no host; o guest so ve o
// handle: ponteiros do host nunca vao para a RAM guest.
//
//   handle = geracao (12 bits, 1..4095) << 20 | indice (20 bits)
//
// 0 nunca e um handle valido. Liberar avanca a geracao do indice: um handle antigo (o guest
// guardou depois de liberar) deixa de resolver em vez de apontar para outro objeto. Um indice
// cuja geracao chega ao fim e aposentado (nunca reaproveitado), para que um handle antigo nao
// volte a valer depois da volta do contador (revisao Codex 2026-10-15). clear() encerra a
// sessao: todos os handles emitidos ficam invalidos.
#include "core/handle.h"

#include <cstdint>
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace bs::transitional {

template <class T>
class HandleTable {
public:
    static constexpr uint32_t kIndexBits  = 20;
    static constexpr uint32_t kIndexMask  = (1u << kIndexBits) - 1;
    static constexpr uint32_t kMaxGen     = 0xFFF;
    static constexpr uint32_t kMaxEntries = kIndexMask + 1;

    // maxGen < kMaxGen limita o handle a menos bits (p.ex. 15: cabe em 24 bits, para quem
    // precisa marcar o valor com uma etiqueta nos bits altos).
    explicit HandleTable(uint32_t maxGen = kMaxGen) : maxGen_(maxGen ? std::min(maxGen, kMaxGen) : 1) {}

    // 0 se a tabela esta cheia.
    uint32_t insert(T value) {
        uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            if (slots_.size() >= kMaxEntries) return 0;
            index = uint32_t(slots_.size());
            slots_.emplace_back();
        }
        Slot& s = slots_[index];
        s.value.emplace(std::move(value));
        ++live_;
        return (s.gen << kIndexBits) | index;
    }

    T* get(uint32_t h) {
        Slot* s = find(h);
        return s ? &*s->value : nullptr;
    }
    const T* get(uint32_t h) const { return const_cast<HandleTable*>(this)->get(h); }

    bool remove(uint32_t h) {
        Slot* s = find(h);
        if (!s) return false;
        release(*s, uint32_t(s - slots_.data()));
        return true;
    }

    // Fim de sessao: invalida todos os handles emitidos.
    void clear() {
        for (uint32_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].value) release(slots_[i], i);
    }

    size_t size() const { return live_; }

    template <class Fn>
    void for_each(Fn&& fn) {
        for (Slot& s : slots_)
            if (s.value) fn(*s.value);
    }

    template <class Tag>
    static native::Handle<Tag> typed(uint32_t h) { return native::Handle<Tag>{h}; }

private:
    struct Slot {
        std::optional<T> value;
        uint32_t         gen = 1;
    };

    Slot* find(uint32_t h) {
        const uint32_t index = h & kIndexMask, gen = h >> kIndexBits;
        if (!h || index >= slots_.size()) return nullptr;
        Slot& s = slots_[index];
        return s.value && s.gen == gen ? &s : nullptr;
    }

    void release(Slot& s, uint32_t index) {
        s.value.reset();
        --live_;
        if (s.gen >= maxGen_) return;  // aposentado
        ++s.gen;
        free_.push_back(index);
    }

    std::vector<Slot>     slots_;
    std::vector<uint32_t> free_;
    size_t                live_ = 0;
    uint32_t              maxGen_ = kMaxGen;
};

} // namespace bs::transitional
