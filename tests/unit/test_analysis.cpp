// Testes do analisador sobre o programa sintetico (tests/fixtures/sh4_fixture.h).
#include "test.h"

#include "../fixtures/sh4_fixture.h"
#include "analysis/analysis.h"
#include "codegen/codegen.h"

using namespace bs;
namespace fx = bs::fixture;

namespace {
analysis::Image fixtureImage() {
    analysis::Image img;
    img.base = fx::kBase;
    for (uint16_t w : fx::kWords) {
        img.bytes.push_back(uint8_t(w));
        img.bytes.push_back(uint8_t(w >> 8));
    }
    return img;
}
analysis::Analysis run() {
    analysis::Config cfg;
    cfg.entry = fx::kMain;
    return analysis::analyze(fixtureImage(), cfg);
}
} // namespace

BS_TEST(analysis_finds_functions_and_calls) {
    const auto a = run();
    BS_CHECK(a.functions.size() == 3);
    BS_CHECK(a.functions.count(fx::kMain) && a.functions.count(fx::kDouble) && a.functions.count(fx::kSet1234));
    BS_CHECK(a.stats.dead_ends == 0 && a.stats.orphans == 0);
    const auto& m = a.functions.at(fx::kMain);
    // jsr @r3 resolvido pelo pool (mov.l + jsr sem label no meio)
    BS_CHECK(m.direct.count(fx::kJsr) && m.direct.at(fx::kJsr) == fx::kSet1234);
    BS_CHECK(m.unresolved.empty());
    // delay slot do bsr fica em slots, nao em insns
    BS_CHECK(m.slots.count(0x8C010010) && !m.insns.count(0x8C010010));
}

BS_TEST(analysis_jump_table_bounded) {
    const auto a = run();
    BS_CHECK(a.jump_tables.size() == 1);
    const auto it = a.jump_tables.find(fx::kBraf);
    BS_CHECK(it != a.jump_tables.end());
    if (it != a.jump_tables.end()) {
        BS_CHECK(it->second.bounded && it->second.table == fx::kTable && it->second.entry_size == 2);
        BS_CHECK(it->second.targets == std::vector<uint32_t>({0x8C010026, 0x8C01002C, 0x8C010034}));
    }
    // a tabela e o pool sao dado, nao codigo
    BS_CHECK(a.data.count(fx::kTable) && a.data.count(0x8C010054) && a.data.count(0x8C010058));
    BS_CHECK(!a.code.count(fx::kTable));
}

BS_TEST(analysis_register_write_sets) {
    using sh4::decode;
    BS_CHECK(analysis::gprWrites(decode(0x64F6)) == ((1u << 4) | (1u << 15)));  // mov.l @r15+,r4
    BS_CHECK(analysis::gprWrites(decode(0x2F46)) == (1u << 15));                // mov.l r4,@-r15
    BS_CHECK(analysis::gprWrites(decode(0x3127)) == 0);                         // cmp/gt
    BS_CHECK(analysis::gprWrites(decode(0xC70B)) == 1);                         // mova -> r0
    BS_CHECK(analysis::gprWrites(decode(0x410E)) == 0xFF);                      // ldc r1,sr (banco)
    BS_CHECK(analysis::gprWrites(decode(0xF00C)) == 0);                         // fmov (FPU)
}

BS_TEST(codegen_emits_expected_constructs) {
    const auto img = fixtureImage();
    const auto a   = run();
    codegen::Options opt;
    const std::string s = codegen::emitFunction(img, a, a.functions.at(fx::kMain), opt);
    BS_CHECK(s.find("switch (::bs::sh4_phys(tgt))") != std::string::npos);
    BS_CHECK(s.find("BS_CALL(BSG_FN(8C01005C)(c), 0x8C010012u)") != std::string::npos);  // bsr double
    BS_CHECK(s.find("BS_EXPECT_TARGET(0x8C01003Cu, tgt, 0x8C010060u)") != std::string::npos);
    BS_CHECK(s.find("c->r[1] = 0x0000012Cu;") != std::string::npos);  // pool inline (300)
    BS_CHECK(s.find("const uint32_t take = c->t;") != std::string::npos);  // bf/s
}

// Revisao Codex M3: rte le SPC antes do slot, restaura SR e verifica o retorno.
BS_TEST(codegen_rte_uses_spc) {
    analysis::Image img;
    img.base = fx::kBase;
    for (uint16_t w : {uint16_t(0x002B), uint16_t(0x0009)}) {  // rte; nop
        img.bytes.push_back(uint8_t(w));
        img.bytes.push_back(uint8_t(w >> 8));
    }
    analysis::Config cfg;
    cfg.entry    = fx::kBase;
    const auto a = analysis::analyze(img, cfg);
    const std::string s = codegen::emitFunction(img, a, a.functions.at(fx::kBase), codegen::Options{});
    const size_t spc = s.find("const uint32_t tgt = c->spc;"), sr = s.find("sh4_set_sr(*c, c->ssr)"),
                 ret = s.find("BS_RTE(0x8C010000u, tgt)");  // rte: retorno conferido ou continuacao/troca de thread
    BS_CHECK(spc != std::string::npos && sr != std::string::npos && ret != std::string::npos);
    BS_CHECK(spc < sr && sr < ret);
}
