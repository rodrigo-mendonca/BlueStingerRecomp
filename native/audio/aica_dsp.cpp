#include "audio/aica_dsp.h"

#include <algorithm>
#include <cstring>

namespace bs::native::audio {

namespace {
int32_t sext(int32_t v, int bits) { return int32_t(uint32_t(v) << (32 - bits)) >> (32 - bits); }
uint32_t u32(std::span<const uint8_t> b, size_t o) {
    return uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24;
}
constexpr size_t kRegs = 0x54;  // byte do SFPB que corresponde ao registrador 0x3000
} // namespace

std::optional<AicaDspProgram> parse_aica_dsp(std::span<const uint8_t> sfpb, std::span<const uint8_t> sfob, std::string* err) {
    auto fail = [&](const char* m) -> std::optional<AicaDspProgram> {
        if (err) *err = m;
        return std::nullopt;
    };
    if (sfpb.size() < kRegs + 0xC00 || std::memcmp(sfpb.data(), "SFPB", 4) != 0) return fail("SFPB curto ou sem assinatura");
    if (sfob.size() < 0x18 || std::memcmp(sfob.data(), "SFOB", 4) != 0) return fail("SFOB curto ou sem assinatura");
    AicaDspProgram p;
    p.rbl = uint8_t(u32(sfpb, 0x34) & 3);
    for (size_t i = 0; i < 128; ++i) p.coef[i] = int16_t(int16_t(u32(sfpb, kRegs + 4 * i) & 0xFFFF) >> 3);
    for (size_t i = 0; i < 64; ++i) p.madrs[i] = uint16_t(u32(sfpb, kRegs + 0x200 + 4 * i));
    for (size_t s = 0; s < 128; ++s) {
        uint64_t v = 0;
        for (size_t k = 0; k < 4; ++k) v = v << 16 | (u32(sfpb, kRegs + 0x400 + 16 * s + 4 * k) & 0xFFFF);
        p.mpro[s] = v;
    }
    // (EFSDL, EFPAN) por saida ate a marca de fim ("ENDB") ou o fim do arquivo
    for (size_t i = 0; i < 16 && 0x18 + 2 * i + 1 < sfob.size(); ++i) {
        if (std::memcmp(sfob.data() + 0x18 + 2 * i, "EN", 2) == 0) break;
        p.efsdl[i] = sfob[0x18 + 2 * i] & 0xF, p.efpan[i] = sfob[0x18 + 2 * i + 1] & 0x1F;
    }
    return p;
}

std::optional<AicaDspProgram> parse_aica_dsp_mlt(std::span<const uint8_t> mlt, std::string* err) {
    // cabecalho do MLT: numero de unidades no byte 8; unidades de 0x20 bytes a partir de 0x20 (tipo, banco, endereco na
    // RAM de som, tamanho, deslocamento no arquivo, tamanho no arquivo)
    std::span<const uint8_t> sfpb, sfob;
    if (mlt.size() >= 0x20) {
        const uint32_t n = u32(mlt, 8);
        for (uint32_t i = 0; i < n && 0x20 + 0x20ull * (i + 1) <= mlt.size(); ++i) {
            const size_t   o = 0x20 + 0x20 * size_t(i);
            const uint32_t off = u32(mlt, o + 16), sz = u32(mlt, o + 20);
            if (off > mlt.size() || sz > mlt.size() - off) continue;
            if (std::memcmp(mlt.data() + o, "SFPB", 4) == 0) sfpb = mlt.subspan(off, sz);
            if (std::memcmp(mlt.data() + o, "SFOB", 4) == 0) sfob = mlt.subspan(off, sz);
        }
    }
    if (sfpb.empty() || sfob.empty()) {
        if (err) *err = "MLT sem SFPB/SFOB";
        return std::nullopt;
    }
    return parse_aica_dsp(sfpb, sfob, err);
}

// Expoente 0..11: numero normalizado, o bit 22 implicito e o inverso do sinal. Expoente 12 (ou mais): pequeno demais
// para normalizar, sem bit implicito (o bit 22 e o proprio sinal) e deslocamento de 11.
int32_t aica_dsp_unpack(uint16_t f) {
    const int32_t sign = f >> 15;
    int32_t       exp  = (f >> 11) & 0xF;
    int32_t       v    = int32_t(f & 0x7FF) << 11 | sign << 23;
    if (exp > 11) exp = 11, v |= sign << 22;
    else v |= (sign ^ 1) << 22;
    return sext(v, 24) >> exp;
}

uint16_t aica_dsp_pack(int32_t v) {
    v                  = sext(v, 24);
    const int32_t sign = (v >> 23) & 1;
    int32_t       exp  = 0;
    // o expoente conta os bits do topo iguais ao sinal (ate 12)
    for (uint32_t t = uint32_t(v ^ (v << 1)) & 0xFFFFFF; exp < 12 && !(t & 0x800000); t <<= 1) ++exp;
    const int32_t mant = exp < 12 ? ((v << exp) & 0x3FFFFF) >> 11 : v & 0x7FF;
    return uint16_t(sign << 15 | exp << 11 | mant);
}

// O anel comeca com o zero do formato da memoria (a palavra 0 seria 0x400000 = meia escala): e o estado do console
// com o programa rodando desde o boot e sem som (o SFPW do MLT so reserva os 128 KB; cada MWT varre o anel inteiro a
// cada 64 K amostras, 1,49 s, entao nenhuma leitura ve um valor que o programa nao gravou).
AicaDsp::AicaDsp(const AicaDspProgram& p) : p_(p), ring_(0x10000, aica_dsp_pack(0)) {}

void AicaDsp::reset() {
    std::fill(ring_.begin(), ring_.end(), aica_dsp_pack(0));
    temp_.fill(0), mems_.fill(0), efreg_.fill(0);
    acc_ = frc_ = adrs_ = yreg_ = read_ = 0;
    mdec_ = 0;
}

void AicaDsp::sample(const std::array<int32_t, 16>& mixs, std::array<int16_t, 16>& efreg) {
    const uint32_t ringMask = (0x2000u << p_.rbl) - 1;
    for (int step = 0; step < 128; ++step) {
        // um passo todo em zero nao e NOP: o ACC recebe TEMP * FRC + TEMP (o B e o X vem do TEMP)
        const uint64_t i = p_.mpro[size_t(step)];
        auto f = [i](int hi, int lo) { return uint32_t(i >> lo) & ((1u << (hi - lo + 1)) - 1); };
        const uint32_t tra = f(63, 57), twt = f(56, 56), twa = f(55, 49), xsel = f(47, 47), ysel = f(46, 45), ira = f(44, 39),
                       iwt = f(38, 38), iwa = f(37, 33), table = f(31, 31), mwt = f(30, 30), mrd = f(29, 29), ewt = f(28, 28),
                       ewa = f(27, 24), adrl = f(23, 23), frcl = f(22, 22), shft = f(21, 20), yrl = f(19, 19), negb = f(18, 18),
                       zero = f(17, 17), bsel = f(16, 16), nofl = f(15, 15), masa = f(14, 9), adreb = f(8, 8), nxadr = f(7, 7);
        // entrada (24 bits): MEMS, MIXS (20 bits) ou EXTS (CD, nao usado: 0)
        int32_t inputs = 0;
        if (ira < 0x20) inputs = mems_[ira];
        else if (ira < 0x30) inputs = sext(mixs[ira - 0x20], 20) << 4;
        // o valor lido dois passos antes vai para MEMS
        if (iwt) mems_[iwa] = read_;
        // B, X e Y; o SHIFTED vem do ACC do passo anterior (antes da multiplicacao deste)
        const int32_t tmp = sext(temp_[(tra + mdec_) & 0x7F], 24);
        int32_t       b   = 0;
        if (!zero) {
            b = bsel ? acc_ : tmp;
            if (negb) b = -b;
        }
        const int32_t x = xsel ? inputs : tmp;
        int32_t       y = 0;
        switch (ysel) {
        case 0: y = sext(frc_, 13); break;
        case 1: y = p_.coef[size_t(step)]; break;
        case 2: y = sext(yreg_ >> 11, 13); break;
        default: y = (yreg_ >> 4) & 0xFFF; break;
        }
        if (yrl) yreg_ = inputs;
        int32_t shifted = 0;
        switch (shft) {
        case 0: shifted = std::clamp(acc_, -0x800000, 0x7FFFFF); break;
        case 1: shifted = std::clamp(acc_ * 2, -0x800000, 0x7FFFFF); break;
        case 2: shifted = sext(acc_ * 2, 24); break;
        default: shifted = sext(acc_, 24); break;
        }
        acc_ = sext(int32_t((int64_t(x) * y) >> 12) + b, 26);
        if (twt) temp_[(twa + mdec_) & 0x7F] = shifted;
        if (frcl) frc_ = shft == 3 ? (shifted & 0xFFF) : ((shifted >> 11) & 0x1FFF);
        if (mrd || mwt) {
            uint32_t a = p_.madrs[masa];
            if (!table) a += mdec_;
            if (adreb) a += uint32_t(adrs_) & 0xFFF;
            if (nxadr) a += 1;
            a &= table ? 0xFFFFu : ringMask;
            if (mrd) read_ = nofl ? sext(int32_t(ring_[a]) << 8, 24) : aica_dsp_unpack(ring_[a]);
            if (mwt) ring_[a] = nofl ? uint16_t(shifted >> 8) : aica_dsp_pack(shifted);
        }
        // A_SEL do manual da AICA: no modo de interpolacao (SHFT = 3) o SHIFTED[23:12]; senao INPUTS[23:16] estendido
        // (revisao Codex 2026-10-21: estava invertido; o programa do Blue Stinger nao usa ADRL)
        if (adrl) adrs_ = shft == 3 ? ((shifted >> 12) & 0xFFF) : ((inputs >> 16) & 0xFFF);
        if (ewt) efreg_[ewa] = int16_t(shifted >> 8);
    }
    --mdec_;
    efreg = efreg_;
}

} // namespace bs::native::audio
