// N3-2: provedor nativo do SDK pd (transitional/pad_adapter) sobre uma RAM de teste: modelo de
// presenca (identificar -> ler; leitura sem controle -> perder), a amostra que chega ao servidor
// (coletada no disparo seguinte), old lido da RAM, bordas, info na area nativa do BIOS HLE e a
// guarda dela, getter, tabela de estado e os negativos.
#include "test.h"

#include "cpu/sh4_context.h"
#include "cpu/sh4_runtime.h"
#include "hle/bios.h"
#include "input/input.h"
#include "input_bridge.h"
#include "pad_adapter.h"
#include "pad_contract.h"
#include "platform/input.h"

#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bs;
namespace ni  = bs::native::input;
namespace pad = bs::transitional::pad;

namespace {

[[noreturn]] void throwingFatal(Sh4Context*, const char* what, uint32_t, uint32_t) { throw std::runtime_error(what); }

struct Ram {
    std::vector<uint8_t> mem = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    Ram() {
        c.ram   = mem.data();
        c.r[15] = 0x8C100000;
        // camada per: per_info(i) = *(*(8C097AAC)) + (int16)*(8C097AA2) + 120 i = 8C2CFFA8 + 120 i
        w32(pad::kPerInfoPtrLiteral, 0x8C2D0C50);
        w32(0x8C2D0C50, 0x8C2CFB5C);
        const uint16_t off = 1100;
        std::memcpy(at(pad::kPerInfoOffLiteral), &off, 2);
        // buffers de info da camada per com "(none)" (porta vazia), como na RAM medida
        for (int i = 0; i < pad::kUnits; ++i) std::memcpy(at(0x8C2CFFA8 + 120u * uint32_t(i) + 0x14), "(none)", 6);
        std::memset(at(kHleNativeAreaBase), 0xFF, kHleNativeAreaSize);  // como o boot nativo deixa
        w32(pad::kPerRawLiteral, 0x8C2B4248);  // per_raw(i) = 8C2B4248 + 24 i (descricoes brutas zeradas)
    }
    uint8_t* at(uint32_t a) { return mem.data() + (a & 0xFFFFFF); }
    void     w32(uint32_t a, uint32_t v) { std::memcpy(at(a), &v, 4); }
    uint32_t r32(uint32_t a) {
        uint32_t v;
        std::memcpy(&v, at(a), 4);
        return v;
    }
    pad::Pds unit(int i) { return pad::pds_load(at(pad::kTable + pad::kUnitSize * uint32_t(i))); }
};

// Fonte de teste por indice de amostra (o "indice leitura" dos roteiros).
ni::SampleFn script(std::map<uint64_t, std::string> steps) {
    return [steps](const ni::SampleContext& ctx) {
        std::string t = "-";
        for (const auto& [k, v] : steps)
            if (k <= ctx.sample) t = v;
        return bs::transitional::input_bridge::to_controller(pad_state_from_text(t));
    };
}

std::string cstr(Ram& r, uint32_t a) { return std::string(reinterpret_cast<const char*>(r.at(a))); }

} // namespace

// Conexao no boot, leitura, desconexao com botao e reconexao: a mesma latencia do legado (o
// servidor ve a amostra de um disparo depois da coleta do disparo seguinte; identificar antes de ler).
BS_TEST(pad_adapter_presence_model_and_server) {
    Ram             r;
    ni::InputSystem sys;
    sys.set_test_source(script({{0, "A+AX=79+LT=200"}, {2, "DESCONECTADO"}, {3, "B"}}));
    pad::test::begin(sys);
    pad::test::server(&r.c);  // antes de qualquer disparo: porta vazia
    pad::Pds u = r.unit(0);
    BS_CHECK(u.info == kHleNativePadInfo && u.id == 0 && u.on == 0 && u.off == 0xFFFFFFFFu && u.name == pad::kNoDeviceName);
    BS_CHECK(cstr(r, kHleNativePadInfo + 0x14) == "(none)" && r.r32(kHleNativePadInfo) == 0);  // copia do buffer da per
    pad::test::dma_fired(&r.c);  // 1: identificar (presente)
    BS_CHECK(r.r32(kHleNativePadInfo) == 0);  // ainda nao coletado
    pad::test::server(&r.c);     // o servidor ainda ve "sem dispositivo"; a coleta vem logo depois dele
    BS_CHECK(r.unit(0).id == 0 && r.r32(kHleNativePadInfo) == 1);
    BS_CHECK(cstr(r, kHleNativePadInfo + 0x14).rfind("Dreamcast Controller", 0) == 0);
    pad::test::dma_fired(&r.c);  // 2: le a amostra 0
    pad::test::server(&r.c);
    BS_CHECK(r.unit(0).id == 0 && r.unit(0).on == 0);  // so a identificacao: "sem dispositivo" no servidor
    pad::test::dma_fired(&r.c);  // 3: coleta a amostra 0; le a 1
    pad::test::server(&r.c);
    u = r.unit(0);
    BS_CHECK(u.id == 0x000F06FEu && u.support == 0x000F06FEu && u.name == kHleNativePadInfo + 0x14);
    BS_CHECK(u.on == (0x4u | 0x20000u) && u.press == u.on && u.release == 0 && u.off == ~u.on);  // A e L >= 128
    BS_CHECK(u.x1 == 79 - 128 && u.y1 == 0 && u.l == 200 && u.r == 0 && u.x2 == 0 && u.y2 == 0);
    BS_CHECK(pad::stats().reads == 2);
    // old vem da RAM: o jogo reescreve on (8C064878); a passada seguinte usa esse valor
    r.w32(pad::kTable + 0x08, 0x4);
    pad::test::dma_fired(&r.c);  // 4: coleta a 1 (A+L); a amostra 2 e DESCONECTADO
    pad::test::server(&r.c);
    u = r.unit(0);
    BS_CHECK(u.old == 0x4 && u.press == 0x20000u && u.release == 0);
    // a leitura sem resposta e coletada logo depois do servidor: o controle some da info antes de o
    // servidor ver a porta vazia (o jogo le info+0 no resto do passo)
    BS_CHECK(r.r32(kHleNativePadInfo) == 0 && cstr(r, kHleNativePadInfo + 0x14) == "(none)");
    pad::test::dma_fired(&r.c);  // 5: identificar (sem controle)
    pad::test::server(&r.c);
    u = r.unit(0);
    BS_CHECK(u.id == 0 && u.on == 0 && u.release == (0x4u | 0x20000u) && u.name == pad::kNoDeviceName);
    BS_CHECK(pad::stats().lost == 1 && pad::stats().reads == 2);
    pad::test::dma_fired(&r.c);  // 6: identificar de novo (amostra 2 ainda e o indice: DESCONECTADO)
    pad::test::dma_fired(&r.c);  // 7
    pad::test::server(&r.c);
    BS_CHECK(r.unit(0).id == 0);
    // demais unidades: info da camada per; subunidade so muda name; tabela de estado zerada
    BS_CHECK(r.unit(6).info == 0x8C2CFFA8u + 120u * 6 && r.unit(6).name == pad::kNoDeviceName);
    BS_CHECK(r.unit(1).info == 0x8C2CFFA8u + 120u && r.unit(1).name == 0x8C2CFFA8u + 120u + 20);
    bool status0 = true;
    for (int i = 0; i < pad::kUnits; ++i) status0 = status0 && r.r32(pad::kStatusTable + 4u * uint32_t(i)) == 0;
    BS_CHECK(status0);
    // as unidades != A0 seguem a descricao bruta da camada per (revisao Codex): resposta FC e
    // desconhecida viram os codigos de estado de 8C15C490
    r.at(0x8C2B4248 + 24 * 7)[4]  = 0xFC;
    r.at(0x8C2B4248 + 24 * 13)[4] = 0x42;
    pad::test::server(&r.c);
    BS_CHECK(r.r32(pad::kStatusTable + 4 * 7) == 0xFF0000FCu && r.r32(pad::kStatusTable + 4 * 13) == 0xFF000000u);
    BS_CHECK(r.r32(pad::kStatusTable) == 0 && r.r32(pad::kStatusTable + 4 * 8) == 0);
    pad::test::end();
}

// Tempo experimental: o legado entrega o fim do DMA na espera seguinte, entao a coleta e no disparo
// seguinte (nao depois do servidor): a desconexao aparece na info um disparo depois.
BS_TEST(pad_adapter_collect_in_experimental_time) {
    Ram             r;
    ni::InputSystem sys;
    sys.set_test_source(script({{0, "A"}, {1, "DESCONECTADO"}}));
    pad::test::begin(sys, pad::kNatNone, false);
    pad::test::dma_fired(&r.c);  // identificar
    pad::test::server(&r.c);
    BS_CHECK(r.r32(kHleNativePadInfo) == 0);  // sem coleta depois do servidor
    pad::test::dma_fired(&r.c);  // coleta a identificacao; le a amostra 0
    BS_CHECK(r.r32(kHleNativePadInfo) == 1);
    pad::test::dma_fired(&r.c);  // coleta A; a amostra 1 e DESCONECTADO
    pad::test::server(&r.c);
    BS_CHECK(r.unit(0).on == 0x4 && r.r32(kHleNativePadInfo) == 1);  // a info ainda mostra o controle
    pad::test::dma_fired(&r.c);  // coleta a leitura sem resposta
    BS_CHECK(r.r32(kHleNativePadInfo) == 0);
    pad::test::server(&r.c);
    BS_CHECK(r.unit(0).id == 0);
    pad::test::end();
}

// Unidade-base de outra porta (B0) pela descricao bruta da camada per (2a revisao Codex): controle com
// erro 0 e resposta 8 e decodificado do quadro (condicao em quadro + 8); com erro, "sem dispositivo";
// teclado classificado e erro fatal.
BS_TEST(pad_adapter_other_port_base_unit) {
    auto setup = [](Ram& r, uint8_t error, uint32_t flags) {
        const uint32_t info = 0x8C2CFFA8 + 120u * 6, raw = 0x8C2B4248 + 24u * 6, frame = 0x8C200000;
        r.w32(info, flags);
        r.w32(info + 4, 0xFE060F00);  // dados da funcao como na RAM -> support 000F06FE
        r.at(raw)[2] = error;
        r.at(raw)[4] = 8;
        r.w32(raw + 8, frame);
        const uint8_t cond[8] = {0xFB, 0xFF, 0, 200, 79, 0x80, 0x80, 0x80};  // A, L = 200, x = 79
        std::memcpy(r.at(frame + 8), cond, 8);
    };
    ni::InputSystem sys;
    {
        Ram r;
        setup(r, 0, 1);
        pad::test::begin(sys);
        pad::test::server(&r.c);
        const pad::Pds u = r.unit(6);
        BS_CHECK(u.id == 0x000F06FEu && u.support == 0x000F06FEu && u.on == (0x4u | 0x20000u) && u.l == 200 &&
                 u.x1 == 79 - 128 && u.name == 0x8C2CFFA8u + 120u * 6 + 20);
        pad::test::end();
    }
    {
        Ram r;
        setup(r, 1, 1);  // erro na descricao bruta
        pad::test::begin(sys);
        pad::test::server(&r.c);
        BS_CHECK(r.unit(6).id == 0 && r.unit(6).on == 0 && r.unit(6).name == pad::kNoDeviceName);
        pad::test::end();
    }
    {
        Ram r;
        setup(r, 0, 0x40);  // teclado Dreamcast
        pad::test::begin(sys);
        sh4_set_fatal_handler(&throwingFatal);
        bool threw = false;
        try {
            pad::test::server(&r.c);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        sh4_set_fatal_handler(nullptr);
        BS_CHECK(threw);
        pad::test::end();
    }
}

// getter: mesmos efeitos que 8C15C668 (r0, MACL e a palavra abaixo da pilha); porta negativa por exts.w.
BS_TEST(pad_adapter_getter) {
    Ram             r;
    ni::InputSystem sys;
    pad::test::begin(sys);
    r.c.r[4] = 2;
    pad::test::getter(&r.c);
    BS_CHECK(r.c.r[0] == pad::kTable + 104 && r.c.macl == 104 && r.r32(0x8C100000 - 4) == 2);
    r.c.r[4] = 0xFFFFFFFFu;
    pad::test::getter(&r.c);
    BS_CHECK(r.c.r[0] == pad::kTable - 52);
    pad::test::end();
}

// Guarda da area nativa: ocupada no comeco ou escrita por outro codigo = erro fatal.
BS_TEST(pad_adapter_native_area_guard) {
    sh4_set_fatal_handler(&throwingFatal);
    auto fails = [](auto&& fn) {
        try {
            fn();
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };
    {
        Ram             r;
        ni::InputSystem sys;
        pad::test::begin(sys);
        r.at(kHleNativeAreaBase)[5] = 0;  // alguem ja usa a faixa
        BS_CHECK(fails([&] { pad::test::server(&r.c); }));
        pad::test::end();
    }
    {
        Ram             r;
        ni::InputSystem sys;
        pad::test::begin(sys);
        pad::test::server(&r.c);
        BS_CHECK(!fails([&] { pad::test::server(&r.c); }));
        r.at(kHleNativePadInfo)[0x20] ^= 1;  // escrita alheia entre duas passadas
        BS_CHECK(fails([&] { pad::test::server(&r.c); }));
        BS_CHECK(pad::stats().guardFaults == 1);
        pad::test::end();
    }
    sh4_set_fatal_handler(nullptr);
    // a reserva fica fora de tudo o que o boot nativo e o HLE definem (0x8C000068-77, vetores
    // 0x8C0000B0-EF, stubs 0x8C001000-10F1, tabela 0x8C003174-3237)
    BS_CHECK(kHleNativeAreaBase >= 0x8C003238 && kHleNativeAreaBase + kHleNativeAreaSize <= 0x8C008000);
    BS_CHECK(kHleNativePadInfoSize == uint32_t(pad::kInfoBytes) && kHleNativePadInfoSize <= kHleNativeAreaSize);
}

// Negativos: cada mutacao muda o que o servidor escreve (a suite de integracao confere que o traco
// acusa); old_host so difere quando o jogo reescreve on (provado aqui).
BS_TEST(pad_adapter_mutations) {
    auto run = [](uint32_t mut, bool gameWritesOn) {
        Ram             r;
        ni::InputSystem sys;
        sys.set_test_source(script({{0, "A"}, {1, "A+B"}, {2, "Y"}}));
        pad::test::begin(sys, mut);
        for (int k = 0; k < 3; ++k) pad::test::dma_fired(&r.c);
        pad::test::server(&r.c);
        if (gameWritesOn) r.w32(pad::kTable + 0x08, 0);
        pad::test::dma_fired(&r.c);
        pad::test::server(&r.c);
        const pad::Pds u = r.unit(0);
        const uint32_t info0 = r.r32(kHleNativePadInfo);
        pad::test::end();
        return std::make_pair(u, info0);
    };
    const auto base = run(pad::kNatNone, true);
    BS_CHECK(base.first.on == 0x6 && base.first.old == 0 && base.first.press == 0x6 && base.second == 1);
    BS_CHECK(run(pad::kNatOldHost, true).first.old == 0x4);       // old do host, nao da RAM
    BS_CHECK(run(pad::kNatOldHost, false).first == run(pad::kNatNone, false).first);  // sem escrita do jogo: igual
    BS_CHECK(run(pad::kNatPressOn, true).first.press == 0x6 && run(pad::kNatPressOn, false).first.press == 0x6);
    BS_CHECK(run(pad::kNatPressOn, false).first.press != run(pad::kNatNone, false).first.press);
    BS_CHECK(run(pad::kNatInfoNoConnect, true).second == 0);
    // amostra adiantada: o servidor ve a amostra 2 (Y) em vez da coletada (A+B)
    BS_CHECK(run(pad::kNatSampleEarly, true).first.on == uint32_t(kPadY) && base.first.on != uint32_t(kPadY));
    BS_CHECK(pad::parse_mutations("old_host,press_on,maple") == (pad::kNatOldHost | pad::kNatPressOn | pad::kNatMaple));
    bool threw = false;
    try {
        pad::parse_mutations("inexistente");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    BS_CHECK(threw);
}
