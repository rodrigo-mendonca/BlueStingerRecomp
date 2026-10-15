#pragma once
// Adaptador temporario Sh4Context <-> interface nativa (segunda camada das fronteiras,
// docs/PLANO-PORT-NATIVO.md secao 2). Um adaptador de fronteira e uma Sh4Func: le os
// argumentos pela convencao do jogo (r4-r7, depois a pilha; floats em fr4-fr11), faz o
// marshaling explicito das estruturas, chama a interface nativa (native/) e escreve o
// retorno (r0/fr0) e as saidas na RAM guest.
//
// O marshaling so toca a RAM principal (area 3). Um endereco de hardware ou fora da RAM e
// recusado: o adaptador nunca vira um caminho para o hardware simulado. Tudo isso some
// quando os chamadores forem elevados (N9).
#include "cpu/sh4_context.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>

namespace bs::transitional {

// Acesso verificado a RAM guest.
class GuestMemory {
public:
    explicit GuestMemory(Sh4Context* c) : c_(c) {}

    // Endereco (P0-P3, espelhos da area 3) -> deslocamento na RAM, se [a, a+n) cabe nela.
    static std::optional<uint32_t> ram_offset(uint32_t a, uint32_t n) {
        if (a >= 0xE0000000u || (a & 0x1C000000u) != 0x0C000000u) return std::nullopt;
        const uint32_t off = a & 0x00FFFFFFu;
        if (n > 0x01000000u || off > 0x01000000u - n) return std::nullopt;
        return off;
    }

    bool read(uint32_t a, std::span<std::byte> out) const {
        const auto off = ram_offset(a, uint32_t(out.size()));
        if (!off) return false;
        std::memcpy(out.data(), c_->ram + *off, out.size());
        return true;
    }
    bool write(uint32_t a, std::span<const std::byte> in) {
        const auto off = ram_offset(a, uint32_t(in.size()));
        if (!off) return false;
        std::memcpy(c_->ram + *off, in.data(), in.size());
        return true;
    }
    template <class T>
    std::optional<T> load(uint32_t a) const {  // T trivial (u8/u16/u32/float), little-endian
        T v;
        if ((a % alignof(T)) || !read(a, std::as_writable_bytes(std::span(&v, 1)))) return std::nullopt;
        return v;
    }
    template <class T>
    bool store(uint32_t a, T v) {
        return !(a % alignof(T)) && write(a, std::as_bytes(std::span(&v, 1)));
    }
    // String terminada em 0, ate `max` bytes (sem o terminador); nullopt sem terminador/fora da RAM.
    std::optional<std::string> cstring(uint32_t a, size_t max) const {
        std::string s;
        for (size_t i = 0; i <= max; ++i) {
            const auto ch = load<uint8_t>(a + uint32_t(i));
            if (!ch) return std::nullopt;
            if (*ch == 0) return s;
            s += char(*ch);
        }
        return std::nullopt;
    }

private:
    Sh4Context* c_;
};

// Argumentos e retorno pela convencao de chamada do jogo (SH-4, compilador SHC/GCC).
class GuestCall {
public:
    explicit GuestCall(Sh4Context* c) : c_(c), mem_(c) {}

    // i-esimo argumento inteiro: r4-r7; os seguintes na pilha, em @(r15 + 4*(i-4)) no ponto da chamada.
    std::optional<uint32_t> arg(int i) const {
        if (i < 4) return c_->r[4 + i];
        return mem_.load<uint32_t>(c_->r[15] + 4u * uint32_t(i - 4));
    }
    // i-esimo argumento float (precisao simples): fr4-fr11.
    std::optional<float> farg(int i) const {
        if (i < 0 || i > 7) return std::nullopt;
        float f;
        std::memcpy(&f, &c_->fr[4 + i], 4);
        return f;
    }
    void ret(uint32_t v) { c_->r[0] = v; }
    void fret(float v) { std::memcpy(&c_->fr[0], &v, 4); }

    GuestMemory&       memory() { return mem_; }
    const GuestMemory& memory() const { return mem_; }

private:
    Sh4Context* c_;
    GuestMemory mem_;
};

// Leitura campo a campo de uma estrutura do guest com layout documentado e versao: o
// tamanho declarado limita os campos (offset fora dele e erro de programacao, nao leitura
// silenciosa de memoria vizinha).
struct GuestLayout {
    const char* name;
    uint32_t    version;
    uint32_t    size;
};

class GuestStruct {
public:
    GuestStruct(const GuestMemory& m, uint32_t base, const GuestLayout& l) : m_(m), base_(base), l_(l) {}
    bool valid() const { return GuestMemory::ram_offset(base_, l_.size).has_value(); }
    template <class T>
    std::optional<T> field(uint32_t offset) const {
        if (offset + sizeof(T) > l_.size) return std::nullopt;
        return m_.load<T>(base_ + offset);
    }
    const GuestLayout& layout() const { return l_; }

private:
    const GuestMemory& m_;
    uint32_t           base_;
    GuestLayout        l_;
};

} // namespace bs::transitional
