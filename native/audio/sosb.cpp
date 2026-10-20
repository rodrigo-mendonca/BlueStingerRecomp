#include "audio/sosb.h"

#include "audio/aica_adpcm.h"

#include <cmath>
#include <cstring>

namespace bs::native::audio {

namespace {
uint32_t u32(const std::vector<uint8_t>& b, size_t o) {
    uint32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
uint16_t u16(const std::vector<uint8_t>& b, size_t o) {
    uint16_t v;
    std::memcpy(&v, b.data() + o, 2);
    return v;
}
bool fits(const std::vector<uint8_t>& b, size_t o, size_t n) { return o <= b.size() && n <= b.size() - o; }
} // namespace

double SosbEffect::rate() const { return 44100.0 * std::pow(2.0, oct) * (1.0 + fns / 1024.0); }

std::optional<SosbBanks> SosbBanks::parse(std::vector<uint8_t> b, std::string* err) {
    auto fail = [&](const std::string& m) -> std::optional<SosbBanks> {
        if (err) *err = m;
        return std::nullopt;
    };
    if (!fits(b, 0, 0x20) || std::memcmp(b.data(), "SMLT", 4) != 0) return fail("nao e um MLT (SMLT)");
    const uint32_t units = u32(b, 8);
    if (units > 64 || !fits(b, 0x20, size_t(units) * 0x20)) return fail("tabela de unidades invalida");
    SosbBanks out;
    for (uint32_t i = 0; i < units; ++i) {
        const size_t e = 0x20 + size_t(i) * 0x20;
        if (std::memcmp(b.data() + e, "SOSB", 4) != 0) continue;
        const uint32_t bank = u32(b, e + 4), off = u32(b, e + 16), size = u32(b, e + 20);
        if (bank > 255 || !fits(b, off, size) || size < 0x10 || std::memcmp(b.data() + off, "SOSB", 4) != 0)
            return fail("unidade SOSB " + std::to_string(i) + " fora do arquivo ou sem assinatura");
        const uint32_t n = u32(b, off + 12);
        if (n > 128 || size < 0x10 + 4ull * n) return fail("SOSB banco " + std::to_string(bank) + ": tabela de pedidos invalida");
        for (uint32_t r = 0; r < n; ++r) {
            const uint32_t d = u32(b, off + 0x10 + 4 * r);
            if (d > size || size - d < 0x38 || std::memcmp(b.data() + off + d, "SOSP", 4) != 0)
                return fail("SOSB banco " + std::to_string(bank) + " pedido " + std::to_string(r) + ": descritor invalido");
            const size_t p = off + d;
            SosbEffect   s;
            s.bank      = uint8_t(bank);
            s.request   = uint8_t(r);
            const uint32_t sa = (uint32_t(b[p + 4]) << 16) | u16(b, p + 6);
            s.flags     = b[p + 5];
            s.baseTl    = b[p + 0x19];
            s.loopStart = u16(b, p + 8);
            s.samples   = u16(b, p + 10);
            const uint32_t pitch = u32(b, p + 16);
            const int      o     = int((pitch >> 11) & 0xF);
            s.oct       = o >= 8 ? o - 16 : o;
            s.fns       = pitch & 0x3FF;
            s.offset    = off + sa;
            if (!fits(b, s.offset, (s.samples + 1) / 2)) return fail("efeito " + std::to_string(s.id()) + ": amostras fora do arquivo");
            out.effects_.push_back(s);
        }
    }
    if (out.effects_.empty()) return fail("nenhuma unidade SOSB");
    out.data_ = std::move(b);
    return out;
}

const SosbEffect* SosbBanks::find(uint8_t bank, uint8_t request) const {
    for (const auto& e : effects_)
        if (e.bank == bank && e.request == request) return &e;
    return nullptr;
}

std::shared_ptr<SoundData> SosbBanks::decode(uint8_t bank, uint8_t request, std::string* err) const {
    const SosbEffect* e = find(bank, request);
    if (!e) {
        if (err) *err = "efeito inexistente";
        return nullptr;
    }
    if (!(e->flags & 1)) {  // so o ADPCM foi provado no oraculo
        if (err) *err = "formato do efeito sem prova (indicadores " + std::to_string(e->flags) + ")";
        return nullptr;
    }
    auto s  = std::make_shared<SoundData>();
    s->rate = uint32_t(std::lround(e->rate()));
    AicaAdpcmState st;
    aica_adpcm_decode(std::span<const uint8_t>(data_.data() + e->offset, (e->samples + 1) / 2), s->pcm, st, e->samples);
    // laco da AICA: LSA..LEA ate o parar (O6 2026-10-20, --trocar: os pedidos 11, 37 e 67 do banco 0 com o bit 1 ligam
    // LPCTL com LSA/LEA do descritor e nao desligam sozinhos; 90 e 117, sem o bit, desligam no fim)
    if ((e->flags & 2) && e->loopStart < e->samples) s->loopStart = e->loopStart, s->loopEnd = e->samples;
    return s;
}

} // namespace bs::native::audio
