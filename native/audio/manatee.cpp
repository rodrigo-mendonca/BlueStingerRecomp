#include "audio/manatee.h"

#include <cmath>
#include <cstring>

namespace bs::native::audio::manatee {

namespace {
uint32_t u32(std::span<const uint8_t> b, size_t o) {
    return uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24;
}
uint16_t u16(std::span<const uint8_t> b, size_t o) { return uint16_t(b[o] | b[o + 1] << 8); }
bool cabe(std::span<const uint8_t> b, size_t o, size_t n) { return o <= b.size() && n <= b.size() - o; }
template <class T>
std::optional<T> fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return std::nullopt;
}

// Le os eventos de [i, fim) como o driver; false com o motivo se cortado ou sem 0x83
bool eventos(std::span<const uint8_t> b, size_t i, size_t fim, std::vector<SeqEvent>& out, std::string& err) {
    auto byte = [&](uint8_t& v) {
        if (i >= fim) return false;
        v = b[i++];
        return true;
    };
    constexpr size_t kMax = 1u << 20;
    while (i < fim && out.size() < kMax) {
        SeqEvent e{};
        e.pos = uint32_t(i);
        uint8_t s, x, y;
        if (!byte(s)) break;
        bool ok = true;
        if (s < 0x80) {
            e.kind = SeqEvent::Kind::Note, e.canal = s & 0x0F;
            ok = byte(e.a) && byte(e.b);
            for (int k = 0; ok && k < (s >> 5) + 1; ++k) ok = byte(x), e.gate = (e.gate << 8) | x;
            ok = ok && byte(x), e.delta = x;
            if (ok && (s & 0x10)) ok = byte(y), e.delta = (e.delta << 8) | y;
        } else if (s == 0x83) {
            e.kind = SeqEvent::Kind::End;
            out.push_back(e);
            return true;
        } else if (s == 0x84) {
            e.kind = SeqEvent::Kind::Tempo;
            ok = byte(x) && byte(y) && byte(e.a);
            e.ref = uint32_t(x) << 8 | y, e.delta = e.a, e.a = 0;
        } else if (s == 0x81) {
            e.kind = SeqEvent::Kind::Reference;
            ok = byte(x) && byte(y) && byte(e.count);
            e.ref = uint32_t(x) << 8 | y;
        } else if (s == 0x82) {
            e.kind = SeqEvent::Kind::Loop;
            ok = byte(x) && byte(y);
            e.count = x & 0x7F, e.delta = y;
            if (ok && (x & 0x80)) ok = byte(y), e.delta = (e.delta << 8) | y;
        } else if (s >= 0xA0 && s < 0xC0) {
            e.kind = SeqEvent::Kind::Control, e.canal = s & 0x0F;
            ok = byte(x) && byte(e.b) && byte(y);
            e.a = x & 0x7F, e.delta = y;
            if (ok && (x & 0x80)) ok = byte(y), e.delta = (e.delta << 8) | y;
        } else if (s >= 0xC0 && s < 0xE0) {
            e.kind = s < 0xD0 ? SeqEvent::Kind::Program : SeqEvent::Kind::Pressure, e.canal = s & 0x0F;
            ok = byte(x) && byte(y);
            e.a = x & 0x7F, e.delta = y;
            if (ok && (x & 0x80)) ok = byte(y), e.delta = (e.delta << 8) | y;
        } else if (s >= 0xE0 && s < 0xF0) {
            e.kind = SeqEvent::Kind::Pitch, e.canal = s & 0x0F;
            ok = byte(e.a) && byte(x), e.delta = x;
        } else {
            e.kind = SeqEvent::Kind::Ignored, e.a = s;  // o driver segue sem delta
        }
        if (!ok) {
            err = "evento cortado em " + std::to_string(e.pos);
            return false;
        }
        out.push_back(e);
    }
    err = "sequencia sem fim (0x83)";
    return false;
}
} // namespace

std::optional<std::vector<Sequence>> parse_smsb(std::span<const uint8_t> b, std::string* err) {
    if (!cabe(b, 0, 16) || std::memcmp(b.data(), "SMSB", 4) != 0) return fail<std::vector<Sequence>>(err, "nao e SMSB");
    const uint32_t n = u32(b, 12);
    if (n == 0 || n > 256 || !cabe(b, 16, 4ull * n)) return fail<std::vector<Sequence>>(err, "SMSB: tabela de sequencias invalida");
    std::vector<Sequence> out;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t o   = u32(b, 16 + 4 * k);
        const uint32_t fim = k + 1 < n ? u32(b, 16 + 4 * (k + 1)) : uint32_t(b.size());
        if (!cabe(b, o, 12) || fim > b.size() || fim < o + 12 || std::memcmp(b.data() + o, "SMSD", 4) != 0)
            return fail<std::vector<Sequence>>(err, "SMSB: sequencia " + std::to_string(k) + " sem SMSD valido");
        Sequence s;
        s.start = o + 12, s.header1 = u32(b, o + 4), s.header2 = u32(b, o + 8);
        // limites da aritmetica do driver (32 bits): nos 11 discos header1 = 136 e o tempo cabe em 2 bytes (o evento
        // 0x84 so tem 2); acima disso unidade * gate (4 bytes) sairia do int64 (revisao Codex 2026-10-20, MEDIA)
        if (s.header1 == 0 || s.header1 > 0xFFFF || s.header2 > 0xFFFF)
            return fail<std::vector<Sequence>>(err, "sequencia " + std::to_string(k) + ": cabecalho de tempo fora dos limites");
        std::string e;
        if (!eventos(b, s.start, fim, s.events, e)) return fail<std::vector<Sequence>>(err, "sequencia " + std::to_string(k) + ": " + e);
        out.push_back(std::move(s));
    }
    return out;
}

std::optional<DriverTables> parse_driver_tables(std::span<const uint8_t> drv, std::string* err) {
    // o arquivo do disco tem 0x20 bytes de cabecalho antes do codigo carregado no endereco 0 da RAM de som
    constexpr size_t kHdr = 0x20, kFns = 0x35E8, kVol = 0x3EC8;
    if (drv.size() < kHdr + kVol + 128) return fail<DriverTables>(err, "MANATEE.DRV: curto demais");
    const auto code = drv.subspan(kHdr);
    for (uint32_t j = 0; j < 1536; ++j) {
        const uint32_t fns = code[kFns + j] + 256u * ((j >= 0x1EF) + (j >= 0x383) + (j >= 0x4D9));  // 0x150C-0x1530
        if (fns != uint32_t(std::floor(1024.0 * (std::exp2(j / 1536.0) - 1.0))))
            return fail<DriverTables>(err, "MANATEE.DRV: versao desconhecida (tabela de FNS)");
    }
    DriverTables t;
    std::memcpy(t.volume.data(), code.data() + kVol, 128);
    for (size_t i = 1; i < 128; ++i)
        if (t.volume[i] < t.volume[i - 1]) return fail<DriverTables>(err, "MANATEE.DRV: curva de volume invalida");
    return t;
}

std::optional<Bank> parse_smpb(std::span<const uint8_t> b, std::string* err) {
    if (!cabe(b, 0, 0x30) || std::memcmp(b.data(), "SMPB", 4) != 0) return fail<Bank>(err, "nao e SMPB");
    Bank bank;
    bank.data = b;
    const uint32_t offProg = u32(b, 0x10), nProg = u32(b, 0x14), offCurv = u32(b, 0x18), nCurv = u32(b, 0x1C);
    if (nProg > 256 || !cabe(b, offProg, 4ull * nProg)) return fail<Bank>(err, "SMPB: tabela de programas invalida");
    if (nCurv > 16 || !cabe(b, offCurv, 128ull * nCurv)) return fail<Bank>(err, "SMPB: curvas de velocidade invalidas");
    for (uint32_t k = 0; k < nCurv; ++k) {
        std::array<uint8_t, 128> c;
        std::memcpy(c.data(), b.data() + offCurv + 128 * k, 128);
        bank.velocityCurves.push_back(c);
    }
    for (uint32_t p = 0; p < nProg; ++p) {
        const uint32_t po = u32(b, offProg + 4 * p);
        if (!cabe(b, po, 8)) return fail<Bank>(err, "SMPB: programa " + std::to_string(p) + " fora do arquivo");
        Program prog;
        // o key-on do driver (0x0B18-0x11FC) percorre 4 ponteiros de grupo (0 = vazio); nos bancos do disco so os 2
        // primeiros sao usados, mas um banco de mod pode usar os 4
        for (uint32_t k = 0; k < 4; ++k) {
            const uint32_t go = cabe(b, po + 4 * k, 4) ? u32(b, po + 4 * k) : 0;
            if (go == 0) continue;
            if (!cabe(b, go, 16)) return fail<Bank>(err, "SMPB: grupo do programa " + std::to_string(p) + " fora do arquivo");
            const uint32_t w = u32(b, go), lo = u32(b, go + 4);
            if (w & 0x80) continue;  // grupo desligado: o driver pula (0x0B60)
            const uint32_t n = w & 0x7F;  // o driver conta so os 7 bits baixos
            if (!cabe(b, lo, 0x30ull * n)) return fail<Bank>(err, "SMPB: camadas do programa " + std::to_string(p) + " fora do arquivo");
            Group g;
            g.flags = u32(b, go + 12);
            for (uint32_t k = 0; k < n; ++k) {
                const uint8_t* d = b.data() + lo + 0x30 * k;
                Layer          c;
                std::memcpy(c.raw, d, 0x30);
                c.sa = (uint32_t(d[0] & 0x7F) << 16) | uint16_t(d[2] | d[3] << 8);
                c.flags = d[1], c.lsa = uint16_t(d[4] | d[5] << 8), c.lea = uint16_t(d[6] | d[7] << 8);
                c.disdl = d[0x13], c.tl = d[0x15];
                c.eg10 = uint16_t(d[8] | d[9] << 8), c.eg14 = uint16_t(d[10] | d[11] << 8);
                c.keyMin = d[0x24], c.keyMax = d[0x25], c.keyBase = d[0x26], c.fine = d[0x27], c.velMin = d[0x2B],
                c.velMax = d[0x2C];
                const uint64_t bytes = (c.flags & 1) ? (uint64_t(c.lea) + 1) / 2 : 2ull * c.lea;
                if (!cabe(b, c.sa, size_t(bytes))) return fail<Bank>(err, "SMPB: amostra do programa " + std::to_string(p) + " fora do arquivo");
                if ((c.flags & 2) && c.lsa >= c.lea) return fail<Bank>(err, "SMPB: laco invalido no programa " + std::to_string(p));
                g.layers.push_back(c);
            }
            prog.groups.push_back(std::move(g));
        }
        bank.programs.push_back(std::move(prog));
    }
    return bank;
}

} // namespace bs::native::audio::manatee
