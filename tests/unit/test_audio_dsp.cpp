// N6 fase G: o DSP de efeitos da AICA (native/audio/aica_dsp), pela especificacao publica. Sinteticos sempre; com
// BS_GAME_DATA tambem o programa do disco (SFPB/SFOB do BS_SND.MLT), conferido contra os registradores do oraculo.
#include "test.h"

#include "audio/aica_dsp.h"
#include "audio/mixer.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace na = bs::native::audio;

namespace {
struct Campo {
    int      hi, lo;
    uint32_t v;
};
uint64_t instr(std::initializer_list<Campo> cs) {
    uint64_t r = 0;
    for (const auto& c : cs) r |= uint64_t(c.v) << c.lo;
    return r;
}
// campos do MPRO (bits)
constexpr int TWT = 56, TWA = 49, XSEL = 47, YSEL = 45, IRA = 39, IWT = 38, IWA = 33, MWT = 30, MRD = 29, EWT = 28, EWA = 24,
              ADRL = 23, SHFT = 20, ZERO = 17, MASA = 9, ADREB = 8;
std::vector<uint8_t> ler(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}
} // namespace

// o formato da memoria (16 bits em ponto flutuante): a volta preserva o sinal e 11 bits de mantissa
BS_TEST(audio_dsp_float_roundtrip) {
    BS_CHECK(na::aica_dsp_unpack(na::aica_dsp_pack(0)) == 0);
    for (int32_t v : {1, -1, 100, -100, 0x1234, -0x1234, 0x7FFFFF, -0x800000, 0x400000, 0x3FF, 0x12345, -0x54321}) {
        const int32_t back = na::aica_dsp_unpack(na::aica_dsp_pack(v));
        const int32_t tol  = std::max<int32_t>(1, std::abs(v) >> 10);  // 11 bits de mantissa
        BS_CHECK(std::abs(back - v) <= tol);
        BS_CHECK((back < 0) == (v < 0) || back == 0);
    }
}

// um passo: X = MIXS 0, Y = COEF; o seguinte escreve o SHIFTED (o ACC do passo anterior) em EFREG 0
BS_TEST(audio_dsp_mac_and_output) {
    na::AicaDspProgram p;
    p.coef[0] = 2048;  // 0,5
    p.mpro[0] = instr({{0, XSEL, 1}, {0, YSEL, 1}, {0, IRA, 0x20}, {0, ZERO, 1}});
    p.mpro[1] = instr({{0, EWT, 1}, {0, EWA, 0}, {0, ZERO, 1}});
    na::AicaDsp             d(p);
    std::array<int32_t, 16> mixs{};
    std::array<int16_t, 16> ef{};
    mixs[0] = 0x10000;  // 20 bits -> INPUTS = 0x100000
    d.sample(mixs, ef);
    BS_CHECK(ef[0] == 0x800);  // (0x100000 * 2048) >> 12 = 0x80000; >> 8
    BS_CHECK(ef[1] == 0);
}

// linha de atraso no anel: grava a entrada (MWT) em MADRS 0 e le (MRD) em MADRS 1 = MADRS 0 + D; o IWT de dois passos
// depois leva a leitura a MEMS 0 e a saida e a entrada de D amostras antes (o contador MDEC_CT desce uma por amostra)
BS_TEST(audio_dsp_ring_delay) {
    constexpr int      D = 37;
    na::AicaDspProgram p;
    p.rbl      = 0;
    p.madrs[0] = 100, p.madrs[1] = 100 + D;
    p.coef[0] = p.coef[4] = 4095;
    p.mpro[0] = instr({{0, XSEL, 1}, {0, YSEL, 1}, {0, IRA, 0x20}, {0, ZERO, 1}});  // ACC = entrada
    p.mpro[1] = instr({{0, MWT, 1}, {0, MASA, 0}, {0, ZERO, 1}});                       // grava o SHIFTED
    p.mpro[3] = instr({{0, MRD, 1}, {0, MASA, 1}, {0, ZERO, 1}});                       // le
    p.mpro[4] = instr({{0, XSEL, 1}, {0, YSEL, 1}, {0, IRA, 0}, {0, ZERO, 1}});         // usa MEMS 0 (ainda o anterior)
    p.mpro[5] = instr({{0, IWT, 1}, {0, IWA, 0}, {0, ZERO, 1}});                        // a leitura do passo 3 -> MEMS 0
    p.mpro[7] = instr({{0, XSEL, 1}, {0, YSEL, 1}, {0, IRA, 0}, {0, ZERO, 1}, {0, MASA, 0}});
    p.coef[7] = 4095;
    p.mpro[8] = instr({{0, EWT, 1}, {0, EWA, 0}, {0, ZERO, 1}});
    na::AicaDsp             d(p);
    std::array<int32_t, 16> mixs{};
    std::array<int16_t, 16> ef{};
    int                     pico = -1;
    for (int t = 0; t < 100; ++t) {
        mixs[0] = t == 0 ? 0x40000 : 0;
        d.sample(mixs, ef);
        if (ef[0] > 1000 && pico < 0) pico = t;
    }
    BS_CHECK(pico == D);
}

// o programa do disco: a imagem dos registradores (byte 0x54 = 0x3000), o anel de 64 K palavras, as quatro saidas; a
// resposta a um impulso no MIXS 0 e uma reverberacao estereo que decai
BS_TEST(audio_dsp_game_program_reverb) {
    const char* gd = std::getenv("BS_GAME_DATA");
    if (!gd) BS_SKIP("BS_GAME_DATA ausente");
    const auto  mlt = ler(std::filesystem::path(gd) / "BSDATA" / "BS_SND.MLT");
    std::string err;
    auto        prog = na::parse_aica_dsp_mlt(mlt, &err);
    BS_CHECK(prog.has_value());
    if (!prog) return;
    BS_CHECK(prog->rbl == 3);
    BS_CHECK(prog->efsdl[0] == 15 && prog->efsdl[1] == 15 && prog->efsdl[2] == 15 && prog->efsdl[3] == 15 && prog->efsdl[4] == 0);
    BS_CHECK(prog->efpan[0] == 0x1F && prog->efpan[1] == 0x0F);
    // registradores lidos no oraculo (passo 1850 da rota LOAD): MADRS 1 = 13234, COEF 6 = 2198, o passo 0
    BS_CHECK(prog->madrs[1] == 13234 && prog->coef[6] == 2198 && prog->mpro[0] == 0x0000B08000020000ull);
    na::AicaDsp             d(*prog);
    std::array<int32_t, 16> mixs{};
    std::array<int16_t, 16> ef{};
    double                  e[4] = {}, l = 0, r = 0;
    int                     primeiro = -1;
    for (int t = 0; t < 4 * 44100; ++t) {
        mixs[0] = t < 64 ? 0x40000 : 0;  // um estalo curto no barramento da musica
        d.sample(mixs, ef);
        const double s = double(ef[0]) * ef[0] + double(ef[1]) * ef[1] + double(ef[2]) * ef[2] + double(ef[3]) * ef[3];
        e[t / 44100] += s;
        l += std::abs(ef[0]) + std::abs(ef[2]), r += std::abs(ef[1]) + std::abs(ef[3]);
        if (primeiro < 0 && s > 0) primeiro = t;
    }
    std::printf("  DSP do disco: primeira saida em %d amostras; energia por segundo %.3g %.3g %.3g %.3g; L/R %.2f\n", primeiro,
                e[0], e[1], e[2], e[3], l / std::max(r, 1.0));
    BS_CHECK(primeiro >= 0 && e[0] > 0);
    BS_CHECK(e[1] < e[0] && e[2] < e[1] && e[3] < e[0] / 100);  // decai
    BS_CHECK(l > 0 && r > 0);                                     // estereo
}

// no mixer: a voz envia pela razao sobre o volume ao barramento escolhido; sem envio nada passa pelo DSP
BS_TEST(audio_dsp_mixer_send) {
    na::AicaDspProgram p;
    p.coef[0] = 4095;
    p.mpro[0] = instr({{0, XSEL, 1}, {0, YSEL, 1}, {0, IRA, 0x21}, {0, ZERO, 1}});  // MIXS 1
    p.mpro[1] = instr({{0, EWT, 1}, {0, EWA, 0}, {0, ZERO, 1}});
    auto s  = std::make_shared<na::SoundData>();
    s->rate = 44100;
    s->pcm.assign(4410, 10000);
    std::array<std::array<float, 2>, 16> g{};
    g[0] = {1.0f, 0.0f};  // EFREG 0 so na esquerda
    for (float send : {0.0f, 0.5f}) {
        na::AudioMixer m(44100, 4);
        m.set_dsp(std::make_shared<na::AicaDsp>(p), g);
        na::PlayParams pp;
        pp.volume = 0.5f, pp.pan = 1.0f;  // a voz direta so na direita
        pp.dspSend = send, pp.dspBus = 1;
        m.play(s, pp);
        std::vector<int16_t> out(2 * 1000);
        m.render(out);
        const int esq = out[2 * 500], dir = out[2 * 500 + 1];
        BS_CHECK(dir > 4900 && dir < 5100);  // 10000 * 0,5
        if (send == 0) BS_CHECK(esq == 0);
        else BS_CHECK(esq > 2400 && esq < 2600);  // 10000 * 0,5 * 0,5, pelo DSP
    }
}

// vetores exatos do formato (revisao Codex 2026-10-21): zero e os pequenos no expoente 12 (sem bit implicito)
BS_TEST(audio_dsp_float_vectors) {
    BS_CHECK(na::aica_dsp_pack(0) == 0x6000 && na::aica_dsp_pack(1) == 0x6001 && na::aica_dsp_pack(-1) == 0xE7FF);
    BS_CHECK(na::aica_dsp_pack(0x7FFFFF) == 0x07FF && na::aica_dsp_pack(-0x800000) == 0x8000);
    BS_CHECK(na::aica_dsp_unpack(0xE7FF) == -1 && na::aica_dsp_unpack(0x6000) == 0 && na::aica_dsp_unpack(0x0000) == 0x400000);
}

// ADRL (A_SEL do manual): SHFT 0 carrega INPUTS[23:16]; SHFT 3 carrega SHIFTED[23:12]. O passo 1 grava a entrada no anel
// em MADRS 0 e carrega o ADRS; o passo 3 le em MADRS 1 + ADRS (ADREB), que so cai no mesmo endereco com a fonte certa
BS_TEST(audio_dsp_adrl_source) {
    for (int modo : {0, 3}) {
        na::AicaDspProgram p;
        p.coef[0] = p.coef[6] = 4095;
        p.mpro[0] = instr({{0, XSEL, 1}, {0, YSEL, 1}, {0, IRA, 0x20}, {0, ZERO, 1}});
        p.mpro[1] = instr({{0, MWT, 1}, {0, MASA, 0}, {0, ADRL, 1}, {0, SHFT, uint32_t(modo)}, {0, IRA, 0x21}, {0, ZERO, 1}});
        p.mpro[3] = instr({{0, MRD, 1}, {0, MASA, 1}, {0, ADREB, 1}, {0, ZERO, 1}});
        p.mpro[5] = instr({{0, IWT, 1}, {0, IWA, 0}, {0, ZERO, 1}});
        p.mpro[6] = instr({{0, XSEL, 1}, {0, YSEL, 1}, {0, IRA, 0}, {0, ZERO, 1}});
        p.mpro[7] = instr({{0, EWT, 1}, {0, EWA, 0}, {0, ZERO, 1}});
        std::array<int32_t, 16> mixs{};
        mixs[0] = 0x40000;  // ACC do passo 0 ~ 0x3FFC00: SHIFTED[23:12] = 0x3FF
        mixs[1] = 0x5000;   // INPUTS = 0x50000: INPUTS[23:16] = 5
        const uint16_t adrs = modo == 3 ? 0x3FF : 5;
        p.madrs[0] = 200, p.madrs[1] = uint16_t(200 - adrs);
        na::AicaDsp             d(p);
        std::array<int16_t, 16> ef{};
        d.sample(mixs, ef);
        BS_CHECK(ef[0] > 15000);  // leu o que o passo 1 gravou (~0x3FFC)
    }
}

// MIXS de 20 bits sem protecao de estouro (manual da AICA): duas vozes de 30 000 enviando cheio passam de 0x7FFFF e o
// valor volta pelo lado negativo
BS_TEST(audio_dsp_mixs_wraps) {
    na::AicaDspProgram p;
    p.coef[0] = 4095;
    p.mpro[0] = instr({{0, XSEL, 1}, {0, YSEL, 1}, {0, IRA, 0x20}, {0, ZERO, 1}});
    p.mpro[1] = instr({{0, EWT, 1}, {0, EWA, 0}, {0, ZERO, 1}});
    auto s  = std::make_shared<na::SoundData>();
    s->rate = 44100;
    s->pcm.assign(4410, 30000);
    std::array<std::array<float, 2>, 16> g{};
    g[0] = {1.0f, 1.0f};
    na::AudioMixer m(44100, 4);
    m.set_dsp(std::make_shared<na::AicaDsp>(p), g);
    na::PlayParams pp;
    pp.volume = 0.0001f, pp.dspSend = 10000.0f;  // direto quase mudo; o envio cheio (30 000 por voz)
    m.play(s, pp), m.play(s, pp);
    std::vector<int16_t> out(2 * 100);
    m.render(out);
    BS_CHECK(out[2 * 50] < -4000);  // 60 000 * 16 = 960 000 > 0x7FFFF: volta para -88 576
}

// rampa de volume (o envelope da musica por amostra dentro do tick; revisao Codex 2026-10-21): de 0 a 1 em 100 quadros
BS_TEST(audio_mixer_volume_ramp) {
    auto s  = std::make_shared<na::SoundData>();
    s->rate = 44100;
    s->pcm.assign(4410, 10000);
    na::AudioMixer m(44100, 4);
    na::PlayParams pp;
    pp.volume = 0.0f;
    const auto id = m.play(s, pp);
    m.set_volume_ramp(id, 1.0f, 100);
    std::vector<int16_t> out(2 * 200);
    m.render(out);
    BS_CHECK(std::abs(out[2 * 49] - 5000) < 200);  // meio da rampa
    BS_CHECK(out[2 * 150] == 10000);                // depois dela, o alvo
    m.set_volume_ramp(id, 0.0f, 100);
    m.set_volume(id, 0.5f);  // set_volume cancela a rampa
    m.render(out);
    BS_CHECK(out[0] == 5000);
}
