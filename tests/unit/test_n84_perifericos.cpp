// N8.4: barramento de perifericos do produto (runtime/system/maple_bus, docs/N8-4-PERIFERICOS.md): a sondagem da camada
// per sem dispositivos, com o mesmo efeito do modelo do Maple sem controle (RAM, MDST, fim do DMA e ISTNRM bit 12,
// contadores) a cada passo, e a recusa explicita de todo pedido que precisaria de um dispositivo.
#include "test.h"

#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "input/input.h"
#include "maple/maple.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"
#include "system/maple_bus.h"

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

struct Bus {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    explicit Bus(const PeriphBackend* b) {
        c.ram = ram.data();
        periph_use(b);
        if (b) maple_set_controller_present(false);  // o modelo como no --controles nativo
        holly_intc_reset();
        holly_sb_reset();  // reinicia o barramento ativo
    }
    ~Bus() {
        maple_set_controller_present(true);
        periph_use(nullptr);
    }
    void     w(uint32_t a, uint32_t v) { sh4_write32(&c, 0xA0000000 | a, v); }
    uint32_t r(uint32_t a) { return sh4_read32(&c, 0xA0000000 | a); }
    // a tabela do jogo (camada per, 8C0975C0): DEVICE_INFO ao dispositivo principal das 4 portas, sem dados
    void table(uint32_t at, uint32_t recv) {
        for (uint32_t p = 0; p < 4; ++p) {
            w(at + 12 * p, (p == 3 ? 0x80000000u : 0) | (p << 16));
            w(at + 12 * p + 4, recv + 0x40 * p);
            w(at + 12 * p + 8, 0x01u | ((((p << 6) | 0x20)) << 8) | ((p << 6) << 16));
        }
    }
};

// Sequencia fixa de acessos (o que a camada per faz por VBlank: MDEN, MDSTAR, MDST = 1, leituras do MDST ate o fim,
// MDST = 1 de novo) e o que se observa: MDST, ISTNRM, buffers de recepcao, contadores.
std::vector<uint64_t> run(const PeriphBackend* b) {
    Bus                   bus(b);
    std::vector<uint64_t> seen;
    bus.w(0x005F6C14, 1);           // MDEN
    bus.w(0x005F6C04, 0x0C100000);  // MDSTAR
    bus.table(0x0C100000, 0x0C200000);
    for (uint32_t i = 0; i < 4; ++i) bus.w(0x0C200000 + 0x40 * i, 0x12345678);
    for (int round = 0; round < 3; ++round) {
        bus.w(0x005F6C18, 1);  // MDST: dispara
        // o fim exato (revisao Codex): 4 descritores sem dados = 4 x (1 + 0 + 1) x 32 bits x 100 = 25 600 ticks
        const uint64_t t0 = bus.c.cycles;
        for (uint64_t at : {uint64_t(0), uint64_t(12800), uint64_t(25599), uint64_t(25600), uint64_t(25601), uint64_t(40000)}) {
            bus.c.cycles = t0 + at;
            seen.push_back(bus.r(0x005F6C18));
            seen.push_back(bus.r(0x005F6900) & kIstMapleDma);
        }
        bus.w(0x005F6900, kIstMapleDma);  // o tratador limpa o bit
        for (uint32_t i = 0; i < 4; ++i) seen.push_back(bus.r(0x0C200000 + 0x40 * i));
        seen.push_back(bus.r(0x005F6C04));
    }
    const MapleCounters m = periph_counters();
    seen.push_back(m.dmas);
    for (int p = 0; p < 4; ++p) seen.push_back(m.devInfo[p]), seen.push_back(m.getCond[p]), seen.push_back(m.other[p]);
    seen.push_back(m.controllerResponses);
    seen.push_back(periph_cond_seq(0));
    return seen;
}

} // namespace

// A sondagem sem dispositivos: o servico nativo e o modelo do Maple sem controle observam o mesmo, passo a passo.
BS_TEST(n84_bus_matches_maple_model_without_controller) {
    const std::vector<uint64_t> nat = run(nullptr), mod = run(maple_bus_backend());
    BS_CHECK(nat.size() == mod.size() && nat.size() > 60);
    BS_CHECK(nat == mod);
    for (size_t i = 0; i < nat.size() && i < mod.size(); ++i)
        if (nat[i] != mod[i]) std::fprintf(stderr, "  observacao %zu: nativo %llx, modelo %llx\n", i,
                                           static_cast<unsigned long long>(nat[i]), static_cast<unsigned long long>(mod[i]));
    // o que se espera, independente do modelo: ocupado ate 25 599 ticks; em 25 600 o fim (MDST 0, ISTNRM bit 12, pelo
    // holly_intc); sem resposta nos 4 buffers; a cada um dos 3 DMAs
    for (size_t r = 0; r < 3; ++r) {
        const size_t b = r * 17;
        BS_CHECK(nat[b + 0] == 1 && nat[b + 1] == 0 && nat[b + 2] == 1 && nat[b + 4] == 1 && nat[b + 5] == 0);
        BS_CHECK(nat[b + 6] == 0 && nat[b + 7] == kIstMapleDma && nat[b + 8] == 0 && nat[b + 9] == kIstMapleDma);
        BS_CHECK(nat[b + 12] == 0xFFFFFFFF && nat[b + 15] == 0xFFFFFFFF);
    }
    const MapleCounters zero{};
    (void)zero;
    BS_CHECK(maple_bus_refused() == 0);
}

// Sem MDEN o disparo e ignorado, como no modelo.
BS_TEST(n84_bus_ignores_start_when_disabled) {
    Bus bus(nullptr);
    bus.w(0x005F6C04, 0x0C100000);
    bus.table(0x0C100000, 0x0C200000);
    bus.w(0x005F6C18, 1);
    BS_CHECK(bus.r(0x005F6C18) == 0 && bus.r(0x0C200000) == 0 && periph_counters().dmas == 0);
}

// Tudo o que precisaria de um dispositivo ou de um modo sem modelo e erro explicito, contado.
BS_TEST(n84_bus_out_of_contract_is_explicit_error) {
    struct Case {
        const char* what;
        uint32_t    instr, hdr, mdtsel;
    } cases[] = {
        {"GET_CONDITION", 0x80000001, 0x01002009, 0},        // condicao do controle (com dados)
        {"GET_CONDITION sem dados", 0x80000000, 0x00002009, 0},
        {"RESET", 0x80000000, 0x00002003, 0},
        {"destino de outra porta", 0x80000000, 0x00006001, 0},
        {"unidade de expansao (VMU)", 0x80000000, 0x00000101, 0},
        {"padrao de transferencia", 0x80000100, 0x00002001, 0},
        {"disparo por VBlank", 0x80000000, 0x00002001, 1},
    };
    uint64_t n = maple_bus_refused();
    for (const auto& k : cases) {
        Bus         bus(nullptr);
        std::string msg = expectFatal([&] {
            bus.w(0x005F6C14, 1);
            bus.w(0x005F6C04, 0x0C100000);
            bus.w(0x0C100000, k.instr);
            bus.w(0x0C100004, 0x0C200000);
            bus.w(0x0C100008, k.hdr);
            bus.w(0x0C10000C, 0x01000000);
            if (k.mdtsel) bus.w(0x005F6C10, 1);
            bus.w(0x005F6C18, 1);
        });
        const bool ok = msg.find("removido do produto no N8.4") != std::string::npos;
        BS_CHECK(ok);
        BS_CHECK(maple_bus_refused() == ++n);
        if (!ok) std::fprintf(stderr, "  %s: [%s]\n", k.what, msg.c_str());
    }
}

// Roteiros "indice transicao": a transicao avanca uma vez por VBlank-In, no primeiro acesso ao MDST, como no modelo.
BS_TEST(n84_bus_transition_tick_like_model) {
    for (const PeriphBackend* b : {static_cast<const PeriphBackend*>(nullptr), maple_bus_backend()}) {
        Bus            bus(b);
        const uint64_t base = input_transition_index();
        bus.r(0x005F6C18);
        BS_CHECK(input_transition_index() == base);
        periph_vblank_in();
        BS_CHECK(input_transition_index() == base);
        bus.r(0x005F6C18);
        bus.r(0x005F6C18);
        BS_CHECK(input_transition_index() == base + 1);
    }
}

// Sessao nova (boot_prepare -> holly_sb_reset): nada do barramento da sessao anterior sobra. Um DMA pendente (fim ainda
// nao entregue) e descartado: MDST 0, nenhuma interrupcao tardia do Maple, registradores e contadores zerados, a
// transicao dos roteiros desarmada. Nos dois provedores, igual.
BS_TEST(n84_bus_reset_discards_pending_dma_and_state) {
    for (const PeriphBackend* b : {static_cast<const PeriphBackend*>(nullptr), maple_bus_backend()}) {
        Bus bus(b);
        bus.w(0x005F6C14, 1);
        bus.w(0x005F6C04, 0x0C100000);
        bus.table(0x0C100000, 0x0C200000);
        bus.w(0x005F6C18, 1);  // DMA em andamento (fim em 25 600 ticks)
        periph_vblank_in();    // transicao armada
        BS_CHECK(bus.r(0x005F6C18) == 1 && periph_counters().dmas == 1);
        holly_intc_reset();
        holly_sb_reset();  // o boot da sessao seguinte
        const uint64_t base = input_transition_index();
        BS_CHECK(bus.r(0x005F6C18) == 0);  // sem DMA pendente; e a leitura nao avanca a transicao
        BS_CHECK(input_transition_index() == base);
        BS_CHECK(bus.r(0x005F6C04) == 0 && bus.r(0x005F6C14) == 0);
        BS_CHECK(periph_counters().dmas == 0 && periph_counters().devInfo[0] == 0);
        bus.c.cycles += 1000000;  // o instante do fim antigo passou: nenhuma interrupcao do Maple acende
        BS_CHECK((bus.r(0x005F6900) & kIstMapleDma) == 0);
        bus.w(0x005F6C18, 1);  // MDEN zerado: o disparo e ignorado
        BS_CHECK(bus.r(0x005F6C18) == 0 && periph_counters().dmas == 0);
    }
}
