// N8.6: area de som do produto (runtime/system/sound_bus, docs/N8-6-AUDIO.md): sem AICA, ARM7 nem driver. A RAM de som, o
// controle e o RTC se comportam como o modelo da AICA (runtime/aica) passo a passo; qualquer outro registrador da AICA,
// tamanho fora do contrato ou a partida do DMA do G2 (so no produto) e erro explicito.
#include "test.h"

#include "aica/aica_regs.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "debug/clock_log.h"
#include "debug/intc_trace.h"
#include "system/sound_bus.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bs;

namespace {

[[noreturn]] void throwingFatal(Sh4Context*, const char* what, uint32_t, uint32_t) { throw std::runtime_error(what); }

template <class F> std::string expectFatal(F&& fn) {
    sh4_set_fatal_handler(throwingFatal);
    std::string msg;
    try {
        fn();
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    sh4_set_fatal_handler(nullptr);
    return msg;
}

// O que o SDK de som faz no modo nativo (8C08EE60, 8C08EBD0, 8C172634): limpa e preenche a RAM de som, le a palavra de
// estado, mexe no controle e le o RTC; mais leituras de 8 e 16 bits e o espelho da RAM.
// Revisao Codex: tambem a instrumentacao que as referencias comparam (BS_CLOCK_LOG AudRd/Aud/Rtc, BS_INTC_TRACE Rtc), em
// ordem: o arquivo do clock_log e o dump do intc_trace de cada provedor.
std::string g_clockText, g_intcText;
std::vector<uint64_t> run(const SoundBusBackend* b) {
    const std::filesystem::path clk = std::filesystem::temp_directory_path() / "bs_n86_clock.tsv";
    clock_log_init(clk.string());
    intc_trace_init((std::filesystem::temp_directory_path() / "bs_n86_intc.tsv").string());
    snd_bus_use(b);
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    snd_bus_reset();
    snd_bus_rtc_set(0x906D38C5, 0);
    std::vector<uint64_t> seen;
    for (uint32_t a = 0xA0800000; a < 0xA0800000 + 0x4000; a += 4) sh4_write32(&c, a, 0x41474553);  // "SEGA"
    sh4_write32(&c, 0xA0810000, 0x12345678);
    sh4_write16(&c, 0xA0810004, 0xBEEF);
    sh4_write8(&c, 0xA0810006, 0x7A);
    seen.push_back(sh4_read32(&c, 0xA0810000));
    seen.push_back(sh4_read32(&c, 0xA0810004));
    seen.push_back(sh4_read16(&c, 0xA0810004));
    seen.push_back(sh4_read8(&c, 0xA0810006));
    seen.push_back(sh4_read32(&c, 0xA0A10000));  // espelho (2 MB adiante)
    seen.push_back(sh4_read32(&c, 0xA0800010));
    sh4_write32(&c, 0xA0702C00, 0x00000001);  // ARM7 em reset
    seen.push_back(sh4_read32(&c, 0xA0702C00));
    sh4_write32(&c, 0xA0702C00, 0x00000300);
    seen.push_back(sh4_read32(&c, 0xA0702C00));
    for (uint64_t t : {uint64_t(0), uint64_t(200000000), uint64_t(3 * 200000000ull + 7)}) {
        c.cycles = t;
        seen.push_back(sh4_read32(&c, 0xA0710000));
        seen.push_back(sh4_read32(&c, 0xA0710004));
    }
    seen.push_back(sh4_read32(&c, 0xA0710008));
    seen.push_back(snd_bus_ram()[0x10000] | (snd_bus_ram()[0x10001] << 8));
    snd_bus_use(nullptr);
    clock_log_vblank(1);
    g_intcText = intc_trace_dump();
    intc_trace_init("");
    clock_log_init("");
    std::ifstream     f(clk);
    std::stringstream ss;
    ss << f.rdbuf();
    g_clockText = ss.str();
    return seen;
}

} // namespace

BS_TEST(n86_sound_area_matches_aica_model) {
    const auto        nat = run(nullptr);
    const std::string natClock = g_clockText, natIntc = g_intcText;
    const auto        mod = run(aica_bus_backend());
    BS_CHECK(natClock == g_clockText && natClock.find("\n1\t") != std::string::npos);  // a linha do VBlank 1
    BS_CHECK(natIntc == g_intcText && !natIntc.empty());
    BS_CHECK(nat == mod && nat.size() == 16);
    for (size_t i = 0; i < nat.size() && i < mod.size(); ++i)
        if (nat[i] != mod[i]) std::fprintf(stderr, "  observacao %zu: nativo %llx, modelo %llx\n", i,
                                           static_cast<unsigned long long>(nat[i]), static_cast<unsigned long long>(mod[i]));
    BS_CHECK(nat[0] == 0x12345678 && nat[2] == 0xBEEF && nat[3] == 0x7A && nat[4] == 0x12345678);
    BS_CHECK(nat[6] == 1 && nat[7] == 0x300);
    BS_CHECK(((nat[12] << 16) | nat[13]) == 0x906D38C8);  // 3 segundos de relogio depois
    BS_CHECK(sound_bus_refused() == 0);
}

// Tudo o que precisaria da AICA (canais, DSP, temporizadores, interrupcoes do ARM7) e erro explicito, contado.
BS_TEST(n86_sound_area_out_of_contract_is_explicit_error) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    snd_bus_reset();
    uint64_t    n = sound_bus_refused();
    std::string m[6];
    m[0] = expectFatal([&] { sh4_write32(&c, 0xA0700000, 0x00000001); });  // canal 0: key on
    m[1] = expectFatal([&] { (void)sh4_read32(&c, 0xA0702800); });          // registrador do DSP/sistema
    m[2] = expectFatal([&] { sh4_write32(&c, 0xA07028A4, 0x40); });          // interrupcao do ARM7
    m[3] = expectFatal([&] { sh4_write16(&c, 0xA0702C00, 1); });            // controle em 16 bits
    m[4] = expectFatal([&] { (void)sh4_read8(&c, 0xA0710000); });           // RTC em 8 bits
    m[5] = expectFatal([&] { sh4_write32(&c, 0xA0703000, 0); });            // DSP
    for (const auto& s : m) BS_CHECK(s.find("removidos do produto no N8.6") != std::string::npos);
    BS_CHECK(sound_bus_refused() == n + 6);
}

#if !BS_VALIDACAO
// No produto, a partida do DMA do G2 (a transferencia para a AICA) e erro; gravar 0 (a tabela do SDK) e ler o estado nao.
BS_TEST(n86_product_g2_dma_start_is_removed) {
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    sh4_write32(&c, 0xA05F7818, 0);
    BS_CHECK(sh4_read32(&c, 0xA05F7818) == 0);
    for (uint32_t a : {0xA05F7818u, 0xA05F7838u, 0xA05F7858u, 0xA05F7878u})
        BS_CHECK(expectFatal([&] { sh4_write32(&c, a, 1); }).find("removidos do produto no N8.6") != std::string::npos);
}
#endif
