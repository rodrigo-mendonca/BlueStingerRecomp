#include "video/sfd_media.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <string_view>

namespace bs::builder::video {

namespace {

bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}
uint16_t be16(std::span<const uint8_t> d, size_t o) { return uint16_t(d[o] << 8 | d[o + 1]); }
uint32_t be32(std::span<const uint8_t> d, size_t o) { return uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3]; }

int64_t readPts(std::span<const uint8_t> d, size_t o) {
    return (int64_t(d[o] >> 1) & 7) << 30 | int64_t(be16(d, o + 1) >> 1) << 15 | int64_t(be16(d, o + 3) >> 1);
}

int16_t clamp16(int32_t v) { return int16_t(std::clamp(v, -32768, 32767)); }

} // namespace

bool demux_sfd(std::span<const uint8_t> f, SfdStreams& out, std::string* err) {
    out = {};
    size_t i = 0;
    // o SFD comeca com um cabecalho proprio ("Sofdec"...) antes do primeiro pack: procura o 1o 0xBA
    while (i + 4 <= f.size() && !(f[i] == 0 && f[i + 1] == 0 && f[i + 2] == 1 && f[i + 3] == 0xBA)) ++i;
    if (i + 4 > f.size()) return fail(err, "nenhum pack MPEG (000001BA) no arquivo");
    while (i + 4 <= f.size()) {
        if (f[i] != 0 || f[i + 1] != 0 || f[i + 2] != 1) {
            // preenchimento de setor (zeros) depois do codigo de fim
            if (std::all_of(f.begin() + ptrdiff_t(i), f.end(), [](uint8_t b) { return b == 0; })) break;
            return fail(err, "codigo de inicio esperado no deslocamento " + std::to_string(i));
        }
        const uint8_t id = f[i + 3];
        if (id == 0xB9) break;  // fim do program stream
        if (id == 0xBA) {
            if (i + 12 > f.size()) return fail(err, "pack truncado");
            if ((f[i + 4] & 0xF0) == 0x20) i += 12;  // MPEG-1
            else if ((f[i + 4] & 0xC0) == 0x40) {     // MPEG-2 (nao esperado no Sofdec, aceito)
                if (i + 14 > f.size()) return fail(err, "pack MPEG-2 truncado");
                i += 14 + (f[i + 13] & 7);
            } else return fail(err, "pack de formato desconhecido");
            ++out.packs;
            continue;
        }
        if (i + 6 > f.size()) return fail(err, "pacote truncado");
        const size_t len = be16(f, i + 4), end = i + 6 + len;
        if (end > f.size()) return fail(err, "pacote PES alem do fim do arquivo");
        if (id == 0xE0 || id == 0xC0) {
            size_t k = i + 6;
            while (k < end && f[k] == 0xFF) ++k;  // stuffing
            if (k < end && (f[k] & 0xC0) == 0x40) k += 2;  // STD buffer
            int64_t pts = -1;
            if (k < end && (f[k] & 0xF0) == 0x20) pts = readPts(f, k), k += 5;
            else if (k < end && (f[k] & 0xF0) == 0x30) pts = readPts(f, k), k += 10;
            else if (k < end && f[k] == 0x0F) k += 1;
            else return fail(err, "cabecalho PES MPEG-1 invalido no deslocamento " + std::to_string(i));
            if (k > end) return fail(err, "cabecalho PES maior que o pacote");
            auto& dst = id == 0xE0 ? out.video : out.audio;
            dst.insert(dst.end(), f.begin() + ptrdiff_t(k), f.begin() + ptrdiff_t(end));
            if (id == 0xE0) {
                ++out.videoPes;
                if (out.firstVideoPts < 0) out.firstVideoPts = pts;
            } else {
                ++out.audioPes;
                if (out.firstAudioPts < 0) out.firstAudioPts = pts;
            }
        }
        i = end;  // 0xBB (sistema), 0xBE (padding), privados: ignorados
    }
    if (out.video.empty()) return fail(err, "sem stream de video (0xE0)");
    return true;
}

void adx_coefficients(uint32_t highpass, uint32_t rate, int32_t& c1, int32_t& c2) {
    const double a = std::numbers::sqrt2 - std::cos(2.0 * std::numbers::pi * double(highpass) / double(rate));
    const double b = std::numbers::sqrt2 - 1.0;
    const double c = (a - std::sqrt((a + b) * (a - b))) / b;
    // arredondados (nao truncados): conferido amostra a amostra contra um decodificador independente
    // nos SFD do disco (E017, E200A: 0 diferencas; com floor o coef2 sai -3284 em vez de -3283)
    c1 = int32_t(std::lround(c * 2.0 * 4096.0));
    c2 = int32_t(std::lround(-(c * c) * 4096.0));
}

bool adx_parse_header(std::span<const uint8_t> d, AdxInfo& h, std::string* err) {
    h = {};
    if (d.size() < 0x14 || be16(d, 0) != 0x8000) return fail(err, "ADX sem a assinatura 0x8000");
    h.dataOffset = uint32_t(be16(d, 2)) + 4;
    h.encoding   = d[4];
    h.blockSize  = d[5];
    h.bitDepth   = d[6];
    h.channels   = d[7];
    h.sampleRate = be32(d, 8);
    h.totalSamples = be32(d, 12);
    h.highpass   = be16(d, 16);
    h.version    = d[18];
    if (h.encoding != 3 || h.blockSize != 18 || h.bitDepth != 4) return fail(err, "ADX de codificacao nao suportada (so tipo 3, blocos de 18, 4 bits)");
    if (h.channels < 1 || h.channels > 2 || h.sampleRate == 0) return fail(err, "ADX com canais ou taxa invalidos");
    if (h.dataOffset > d.size()) return fail(err, "ADX com dados alem do fim");
    if (d.size() > 19 && d[19] != 0) return fail(err, "ADX cifrado (nao suportado)");
    if (h.dataOffset >= 6 && std::string_view(reinterpret_cast<const char*>(d.data()) + h.dataOffset - 6, 6) != "(c)CRI")
        return fail(err, "ADX sem a marca (c)CRI antes dos dados");
    adx_coefficients(h.highpass, h.sampleRate, h.coef1, h.coef2);
    return true;
}

bool adx_decode(std::span<const uint8_t> d, std::vector<int16_t>& pcm, AdxInfo* infoOut, std::string* err) {
    AdxInfo h;
    if (!adx_parse_header(d, h, err)) return false;
    if (infoOut) *infoOut = h;
    pcm.assign(size_t(h.totalSamples) * h.channels, 0);
    int32_t hist[2][2] = {};
    size_t  o = h.dataOffset;
    for (uint32_t done = 0; done < h.totalSamples; done += 32) {
        for (uint8_t ch = 0; ch < h.channels; ++ch, o += 18) {
            if (o + 18 > d.size()) return fail(err, "ADX truncado antes do total de amostras");
            const uint16_t scaleRaw = be16(d, o);
            if (scaleRaw & 0x8000) return fail(err, "ADX: marca de fim antes do total de amostras");
            const int32_t scale = scaleRaw;
            for (int k = 0; k < 32; ++k) {
                const uint8_t b      = d[o + 2 + size_t(k / 2)];
                const int32_t nibble = (k & 1) ? int32_t(int8_t(b << 4)) >> 4 : int32_t(int8_t(b)) >> 4;
                const int32_t pred   = (h.coef1 * hist[ch][0] + h.coef2 * hist[ch][1]) >> 12;
                const int16_t s      = clamp16(nibble * scale + pred);
                hist[ch][1] = hist[ch][0];
                hist[ch][0] = s;
                const uint32_t n = done + uint32_t(k);
                if (n < h.totalSamples) pcm[size_t(n) * h.channels + ch] = s;
            }
        }
    }
    return true;
}

std::vector<uint8_t> adx_encode(std::span<const int16_t> pcm, uint8_t channels, uint32_t rate) {
    const uint32_t total = uint32_t(pcm.size() / channels);
    const uint16_t hp    = 500;
    int32_t        c1, c2;
    adx_coefficients(hp, rate, c1, c2);
    std::vector<uint8_t> out(0x24, 0);
    auto put16 = [&](size_t o, uint16_t v) { out[o] = uint8_t(v >> 8), out[o + 1] = uint8_t(v); };
    auto put32 = [&](size_t o, uint32_t v) { put16(o, uint16_t(v >> 16)), put16(o + 2, uint16_t(v)); };
    put16(0, 0x8000);
    put16(2, 0x20);  // dados em 0x24
    out[4] = 3, out[5] = 18, out[6] = 4, out[7] = channels;
    put32(8, rate), put32(12, total);
    put16(16, hp);
    out[18] = 3;
    std::memcpy(out.data() + 0x1E, "(c)CRI", 6);
    int32_t hist[2][2] = {};
    for (uint32_t done = 0; done < total; done += 32) {
        for (uint8_t ch = 0; ch < channels; ++ch) {
            auto at = [&](uint32_t n) { return n < total ? int32_t(pcm[size_t(n) * channels + ch]) : 0; };
            // escala pelo maior residuo do bloco com o preditor do historico atual (aproximado)
            int32_t maxr = 0, h0 = hist[ch][0], h1 = hist[ch][1];
            for (int k = 0; k < 32; ++k) {
                const int32_t p = (c1 * h0 + c2 * h1) >> 12;
                maxr = std::max(maxr, std::abs(at(done + uint32_t(k)) - p));
                h1 = h0, h0 = at(done + uint32_t(k));
            }
            const int32_t scale = std::clamp((maxr + 6) / 7, 1, 0x7FFF);
            const size_t  base  = out.size();
            out.resize(base + 18, 0);
            put16(base, uint16_t(scale));
            for (int k = 0; k < 32; ++k) {
                const int32_t p = (c1 * hist[ch][0] + c2 * hist[ch][1]) >> 12;
                int32_t       q = int32_t(std::lround(double(at(done + uint32_t(k)) - p) / scale));
                q               = std::clamp(q, -8, 7);
                const int16_t s = clamp16(q * scale + p);  // o decodificador reconstroi exatamente isto
                hist[ch][1] = hist[ch][0], hist[ch][0] = s;
                out[base + 2 + size_t(k / 2)] |= (k & 1) ? uint8_t(q & 0xF) : uint8_t((q & 0xF) << 4);
            }
        }
    }
    // bloco de fim (escala com o bit 15), como o codificador da CRI
    for (uint8_t ch = 0; ch < channels; ++ch) {
        const size_t base = out.size();
        out.resize(base + 18, 0);
        put16(base, 0x8001);
    }
    return out;
}

} // namespace bs::builder::video
