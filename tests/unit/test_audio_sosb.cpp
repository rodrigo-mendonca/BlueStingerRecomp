// N6 fase C: os efeitos do BS_SND.MLT por (banco, pedido) (native/audio/sosb). Sinteticos sempre; com
// BS_GAME_DATA=<game-data> tambem o arquivo real do usuario, conferido contra o oraculo O6.
#include "test.h"

#include "audio/aica_adpcm.h"
#include "audio/sosb.h"
#include "audio/sound_file.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace na = bs::native::audio;

namespace {
void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) { std::memcpy(b.data() + o, &v, 4); }
void put16(std::vector<uint8_t>& b, size_t o, uint16_t v) { std::memcpy(b.data() + o, &v, 2); }
// MLT minimo: uma unidade SOSB (banco 1) com 2 pedidos; o pedido 0 e uma rampa em ADPCM (indicador 1), o pedido 1
// tem o indicador 3 e laco em 100
std::vector<uint8_t> fakeMlt(const std::vector<uint8_t>& adpcm, uint32_t samples) {
    const size_t unitOff = 0x40, descA = 0x18, descB = descA + 0x38, dataOff = descB + 0x38;
    std::vector<uint8_t> b(unitOff + dataOff + adpcm.size(), 0);
    std::memcpy(b.data(), "SMLT", 4);
    put32(b, 8, 1);
    std::memcpy(b.data() + 0x20, "SOSB", 4);
    put32(b, 0x24, 1), put32(b, 0x28, 0x1691E0), put32(b, 0x2C, uint32_t(b.size() - unitOff));
    put32(b, 0x30, uint32_t(unitOff)), put32(b, 0x34, uint32_t(b.size() - unitOff));
    uint8_t* u = b.data() + unitOff;
    std::memcpy(u, "SOSB", 4);
    put32(b, unitOff + 4, 1), put32(b, unitOff + 8, uint32_t(b.size() - unitOff)), put32(b, unitOff + 12, 2);
    put32(b, unitOff + 0x10, uint32_t(descA)), put32(b, unitOff + 0x14, uint32_t(descB));
    for (size_t d : {descA, descB}) {
        std::memcpy(u + d, "SOSP", 4);
        u[d + 4] = uint8_t(dataOff >> 16);
        u[d + 5] = d == descA ? 1 : 3;
        u[d + 0x19] = d == descA ? 40 : 20;  // nivel base
        put16(b, unitOff + d + 6, uint16_t(dataOff & 0xFFFF));
        put16(b, unitOff + d + 8, d == descA ? 0 : 100);
        put16(b, unitOff + d + 10, uint16_t(samples));
        put32(b, unitOff + d + 16, (0xEu << 11) | 833);  // OCT -2, FNS 833 (o ID 117 medido)
    }
    std::memcpy(u + dataOff, adpcm.data(), adpcm.size());
    return b;
}
} // namespace

// (banco, pedido) -> descritor -> amostras: decodifica o mesmo ADPCM que entrou, taxa pela formula da AICA,
// laco so com o indicador 3 (hipotese marcada); estruturas invalidas recusadas com motivo
BS_TEST(audio_sosb_synthetic) {
    std::vector<int16_t> ramp(400);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = int16_t(int(i) * 40 - 8000);
    na::AicaAdpcmState enc;
    const auto         adpcm = na::aica_adpcm_encode(ramp, enc);
    std::string        err;
    auto               m = na::SosbBanks::parse(fakeMlt(adpcm, 400), &err);
    BS_CHECK(m && m->size() == 2);
    const auto* e = m ? m->find(1, 0) : nullptr;
    BS_CHECK(e && e->id() == 128 && e->samples == 400 && e->oct == -2 && e->fns == 833 && e->baseTl == 40);
    BS_CHECK(m && m->find(1, 1) && m->find(1, 1)->baseTl == 20);
    BS_CHECK(e && std::abs(e->rate() - 44100 * 0.25 * (1 + 833 / 1024.0)) < 1e-6);
    auto s = m ? m->decode(1, 0) : nullptr;
    na::AicaAdpcmState dec;
    std::vector<int16_t> ref;
    na::aica_adpcm_decode(adpcm, ref, dec, 400);
    BS_CHECK(s && s->pcm == ref && s->loopStart == -1 && s->rate == uint32_t(std::lround(e->rate())));
    auto l = m ? m->decode(1, 1) : nullptr;
    BS_CHECK(l && l->loopStart == 100 && l->loopEnd == 400);
    BS_CHECK(m && !m->find(0, 0) && !m->decode(1, 2, &err));
    // corrupcoes: assinatura, descritor fora, amostras fora do arquivo
    auto bad = fakeMlt(adpcm, 400);
    bad[0]   = 'X';
    BS_CHECK(!na::SosbBanks::parse(bad, &err) && err.find("SMLT") != std::string::npos);
    bad = fakeMlt(adpcm, 400);
    put32(bad, 0x40 + 0x10, 0x7FFFFFF0);
    BS_CHECK(!na::SosbBanks::parse(bad, &err) && err.find("descritor") != std::string::npos);
    bad = fakeMlt(adpcm, 60000);
    BS_CHECK(!na::SosbBanks::parse(bad, &err) && err.find("fora do arquivo") != std::string::npos);
}

// O arquivo real do usuario (opcional): 211 efeitos, os IDs 90 e 117 com os valores medidos no oraculo O6 e o
// audio decodificado plausivel (nao silencio, pouca saturacao)
BS_TEST(audio_sosb_real_disc_matches_o6) {
    const char* gd = std::getenv("BS_GAME_DATA");
    if (!gd) {
        std::printf("  (sem BS_GAME_DATA: so os sinteticos)\n");
        return;
    }
    std::ifstream f(std::filesystem::path(gd) / "BSDATA" / "BS_SND.MLT", std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    std::string          err;
    auto                 m = na::SosbBanks::parse(std::move(b), &err);
    BS_CHECK(m && m->size() == 211);
    if (!m) return;
    const auto* e117 = m->find(0, 117);
    const auto* e90  = m->find(0, 90);
    // SA na RAM de som = inicio da unidade SOSB#0 (0x097A60) + SA relativo; aqui o deslocamento no arquivo
    BS_CHECK(e117 && e117->samples == 0xE25B && e117->oct == -2 && e117->fns == 833 && e117->offset == 0x7FB60 + 0x0B92DC);
    BS_CHECK(e90 && e90->samples == 0x3080 && e90->oct == -2 && e90->fns == 0 && e90->offset == 0x7FB60 + 0x087FC4);
    for (const auto* e : {e117, e90}) {
        auto s = m->decode(e->bank, e->request, &err);
        BS_CHECK(s && s->frames() == int64_t(e->samples));
        if (!s) continue;
        double sum = 0;
        size_t sat = 0;
        for (int16_t v : s->pcm) sum += double(v) * v, sat += (v == 32767 || v == -32768);
        const double rms = std::sqrt(sum / double(s->pcm.size()));
        std::printf("  efeito %u: %lld amostras a %u Hz, RMS %.0f, saturadas %zu\n", e->id(), static_cast<long long>(s->frames()), s->rate, rms, sat);
        BS_CHECK(rms > 100 && sat < s->pcm.size() / 100);
    }
    // O6 com --trocar (2026-10-20, n6_o6/laco.tsv): key-on de cada pedido com TL (volume 0), LPCTL, LSA e LEA
    struct Medido {
        uint8_t req, tl;
        bool    loop;
        int64_t lsa, lea;
    };
    const Medido medidos[] = {{117, 30, false, 0, 0xE25B}, {90, 30, false, 0, 0x3080}, {11, 40, true, 0x03F7, 0x0E83},
                              {67, 20, true, 0x8691, 0x98C4}, {37, 45, true, 0, 0x1236}};
    for (const auto& x : medidos) {
        const auto* e = m->find(0, x.req);
        auto        s = e ? m->decode(0, x.req, &err) : nullptr;
        BS_CHECK(e && s && e->baseTl == x.tl && e->samples == uint32_t(x.lea));
        if (!s) continue;
        BS_CHECK(x.loop ? (s->loopStart == x.lsa && s->loopEnd == x.lea) : s->loopStart == -1);
    }
    size_t lacos = 0;  // os 7 do inventario (indicador 3)
    for (uint8_t bank : {0, 1})
        for (int r = 0; r < 128; ++r)
            if (const auto* e = m->find(bank, uint8_t(r)); e && (e->flags & 2)) ++lacos;
    BS_CHECK(lacos == 7);
}

// Legendas EN (docs/LEGENDAS-EN-PLANO.md): a copia em Python do decodificador (tools/dev/legendas/catalogo.py) tem que
// dar o MESMO PCM que o do jogo (load_sound). FNV-1a 64 das amostras de 16 bits (little-endian) de duas falas reais;
// os mesmos valores estao em tools/dev/legendas/test_legendas.py. Com BS_GAME_DATA=<disco> (pasta com BSDATA).
BS_TEST(audio_voice_wav_decode_matches_pipeline) {
    const char* gd = std::getenv("BS_GAME_DATA");
    if (!gd) BS_SKIP("BS_GAME_DATA ausente");
    struct Caso {
        const char* nome;
        size_t      amostras;
        uint64_t    fnv;
    };
    for (const Caso& c : {Caso{"E002A.WAV", 460464, 0xdf82023065f43bd5ull}, Caso{"E012BENV.WAV", 265214, 0x38b40368024de61aull}}) {
        std::string err;
        auto        s = na::load_sound(std::filesystem::path(gd) / "BSDATA" / c.nome, &err);
        BS_CHECK(s && s->pcm.size() == c.amostras && s->rate == 22050);
        if (!s) continue;
        uint64_t h = 0xcbf29ce484222325ull;
        for (int16_t v : s->pcm)
            for (int k = 0; k < 2; ++k) h = (h ^ uint8_t(uint16_t(v) >> (8 * k))) * 0x100000001b3ull;
        BS_CHECK(h == c.fnv);
        if (h != c.fnv) std::printf("  %s: FNV %016llx\n", c.nome, static_cast<unsigned long long>(h));
    }
}
