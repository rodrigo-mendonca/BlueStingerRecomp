// N6 fase E: FLAC de mods (native/audio/sound_flac.cpp, dr_flac). O FLAC de teste e montado aqui (STREAMINFO e
// quadros VERBATIM com CRC-8/CRC-16 corretos), sem codificador externo: a decodificacao tem que devolver as mesmas
// amostras; arquivos corrompidos, truncados ou fora dos limites sao recusados com motivo; OGG continua erro explicito.
#include "test.h"

#include "audio/sound_file.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace na = bs::native::audio;

namespace {
uint8_t crc8(const std::vector<uint8_t>& b, size_t from) {
    uint8_t c = 0;
    for (size_t i = from; i < b.size(); ++i) {
        c ^= b[i];
        for (int k = 0; k < 8; ++k) c = uint8_t(c & 0x80 ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}
uint16_t crc16(const std::vector<uint8_t>& b, size_t from) {
    uint16_t c = 0;
    for (size_t i = from; i < b.size(); ++i) {
        c ^= uint16_t(b[i]) << 8;
        for (int k = 0; k < 8; ++k) c = uint16_t(c & 0x8000 ? (c << 1) ^ 0x8005 : c << 1);
    }
    return c;
}
// FLAC mono 16 bits a 44100 Hz, blocos VERBATIM de `block` amostras
std::vector<uint8_t> flac(const std::vector<int16_t>& pcm, uint16_t block) {
    std::vector<uint8_t> b = {'f', 'L', 'a', 'C', 0x80, 0, 0, 34};
    const uint64_t n = pcm.size();
    b.push_back(uint8_t(block >> 8)), b.push_back(uint8_t(block));  // min/max blocksize
    b.push_back(uint8_t(block >> 8)), b.push_back(uint8_t(block));
    for (int i = 0; i < 6; ++i) b.push_back(0);  // min/max framesize: desconhecidos
    // taxa (20) | canais-1 (3) | bits-1 (5) | total de amostras (36)
    const uint64_t v = (uint64_t(44100) << 44) | (uint64_t(0) << 41) | (uint64_t(15) << 36) | n;
    for (int i = 7; i >= 0; --i) b.push_back(uint8_t(v >> (8 * i)));
    for (int i = 0; i < 16; ++i) b.push_back(0);  // MD5 desconhecido
    for (size_t at = 0, frame = 0; at < n; at += block, ++frame) {
        const size_t len = std::min<size_t>(block, n - at);
        const size_t f0  = b.size();
        b.insert(b.end(), {0xFF, 0xF8, 0x79, 0x08, uint8_t(frame), uint8_t((len - 1) >> 8), uint8_t(len - 1)});
        b.push_back(crc8(b, f0));
        b.push_back(0x02);  // subquadro VERBATIM
        for (size_t i = 0; i < len; ++i) b.push_back(uint8_t(uint16_t(pcm[at + i]) >> 8)), b.push_back(uint8_t(pcm[at + i]));
        const uint16_t c = crc16(b, f0);
        b.push_back(uint8_t(c >> 8)), b.push_back(uint8_t(c));
    }
    return b;
}
std::vector<int16_t> ramp(size_t n) {
    std::vector<int16_t> r(n);
    for (size_t i = 0; i < n; ++i) r[i] = int16_t(int(i * 97 % 60000) - 30000);
    return r;
}
} // namespace

BS_TEST(audio_flac_decode_exact) {
    const auto pcm = ramp(1000);
    std::string err;
    auto        s = na::parse_flac(flac(pcm, 256), &err);
    BS_CHECK(s && s->rate == 44100 && s->channels == 1 && s->pcm == pcm);
    if (!s) std::printf("  %s\n", err.c_str());
}

BS_TEST(audio_flac_rejects_bad_input) {
    const auto  good = flac(ramp(600), 256);
    std::string err;
    auto        bad = good;
    bad[0]          = 'X';  // assinatura
    BS_CHECK(!na::parse_flac(bad, &err) && !err.empty());
    BS_CHECK(!na::parse_flac(std::vector<uint8_t>(good.begin(), good.begin() + 20), &err));  // STREAMINFO cortado
    std::vector<uint8_t> empty;
    BS_CHECK(!na::parse_flac(empty, &err));
    // corpo corrompido: o CRC-16 recusa o quadro; o que sobra (antes dele) e devolvido, nunca lixo
    bad = good;
    bad[good.size() - 300] ^= 0x55;
    auto t = na::parse_flac(bad, &err);
    BS_CHECK(!t || t->pcm.size() < 600);
    // muitos canais: STREAMINFO com 8 canais
    bad      = good;
    bad[20] |= 0x0E;  // canais-1 = 7 (bits 3-1 do byte 20)
    BS_CHECK(!na::parse_flac(bad, &err));
}

// pela extensao, num arquivo: FLAC decodifica; OGG e erro explicito (decodificador pendente); WAV continua igual
BS_TEST(audio_flac_load_by_extension) {
    const auto dir = std::filesystem::temp_directory_path() / "bs_audio_flac_test";
    std::filesystem::create_directories(dir);
    const auto pcm = ramp(300);
    {
        const auto    b = flac(pcm, 128);
        std::ofstream(dir / "voz.flac", std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
        std::ofstream(dir / "voz.ogg", std::ios::binary) << "OggS";
    }
    std::string err;
    auto        s = na::load_sound(dir / "voz.flac", &err);
    BS_CHECK(s && s->pcm == pcm);
    BS_CHECK(!na::load_sound(dir / "voz.ogg", &err) && err.find("OGG") != std::string::npos);
    std::filesystem::remove_all(dir);
}

// orcamento de PCM (revisao Codex 2026-10-20): um FLAC minusculo que declara 29 min de estereo a 192 kHz (dentro do
// limite de duracao) e recusado antes de alocar; o arquivo nao chega a 200 bytes
BS_TEST(audio_flac_pcm_budget) {
    auto b = flac(ramp(10), 16);
    // STREAMINFO: taxa 192000 (20 bits), 2 canais (3 bits: 1), 16 bits (5 bits: 15), total 29 min * 192000
    const uint64_t total = 29ull * 60 * 192000;
    const uint64_t v     = (uint64_t(192000) << 44) | (uint64_t(1) << 41) | (uint64_t(15) << 36) | total;
    for (int i = 0; i < 8; ++i) b[8 + 10 + i] = uint8_t(v >> (8 * (7 - i)));
    std::string err;
    BS_CHECK(b.size() < 200 && !na::parse_flac(b, &err) && err.find("decodificado") != std::string::npos);
}
