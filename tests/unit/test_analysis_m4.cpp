// Casos do analisador que surgiram no boot real (M4), com programas SH-4 minimos.
#include "test.h"

#include "analysis/analysis.h"

#include <initializer_list>

using namespace bs;

namespace {

constexpr uint32_t kBase = 0x8C010000;

analysis::Image image(std::initializer_list<uint16_t> words) {
    analysis::Image img;
    img.base = kBase;
    for (uint16_t w : words) {
        img.bytes.push_back(uint8_t(w));
        img.bytes.push_back(uint8_t(w >> 8));
    }
    return img;
}

analysis::Analysis analyze(const analysis::Image& img, std::initializer_list<uint32_t> extra = {}) {
    analysis::Config cfg;
    cfg.entry = kBase;
    cfg.extra_functions.assign(extra.begin(), extra.end());
    cfg.pointer_candidates = cfg.data_pointers = false;
    return analysis::analyze(img, cfg);
}

} // namespace

// 8C17BE08: "mova L,r0; mov.l #0xA0000000,r1; or r0,r1; jmp @r1" salta para o espelho
// P2 do proprio codigo; o alvo normalizado (8C...) vira funcao com tail call direta.
BS_TEST(analysis_or_of_constants_resolves_p2_mirror_jump) {
    const auto img = image({
        0xC703,          // 8C010000 mova 0x8C010010,r0
        0xD102,          // 8C010002 mov.l 0x8C01000C,r1  (= 0xA0000000)
        0x210B,          // 8C010004 or r0,r1
        0x412B,          // 8C010006 jmp @r1
        0x0009,          // 8C010008 nop (slot)
        0x0009,          // 8C01000A
        0x0000, 0xA000,  // 8C01000C pool: 0xA0000000
        0x000B,          // 8C010010 L: rts
        0x0009,          // 8C010012 nop
    });
    const auto a = analyze(img);
    BS_CHECK(a.functions.count(0x8C010010));
    const auto& f = a.functions.at(kBase);
    BS_CHECK(f.direct.count(0x8C010006) && f.direct.at(0x8C010006) == 0x8C010010);
    BS_CHECK(f.tail_calls.count(0x8C010006) && f.unresolved.empty());
}

// 8C08D1D0: copia desenrolada "mov.l #tabela,r3; mov.l @(r0,r3),r3; add #-4,r0; jmp @r3"
// com a base da tabela no pool (nao mova). Destinos locais viram labels.
BS_TEST(analysis_pool_based_jump_table) {
    const auto img = image({
        0xD303,          // 8C010000 mov.l 0x8C010010,r3  (tabela)
        0x033E,          // 8C010002 mov.l @(r0,r3),r3
        0x70FC,          // 8C010004 add #-4,r0
        0x432B,          // 8C010006 jmp @r3
        0x0009,          // 8C010008 nop (slot)
        0x7101,          // 8C01000A add #1,r1   (alvo 1)
        0x7101,          // 8C01000C add #1,r1   (alvo 0)
        0x000B,          // 8C01000E rts
        0x0014, 0x8C01,  // 8C010010 pool: 0x8C010014 (tabela)
        0x000C, 0x8C01,  // 8C010014 tabela[0] = 8C01000C
        0x000A, 0x8C01,  // 8C010018 tabela[1] = 8C01000A
        0xFFFF, 0xFFFF,  // 8C01001C fim (endereco invalido)
    });
    const auto a  = analyze(img);
    const auto it = a.jump_tables.find(0x8C010006);
    BS_CHECK(it != a.jump_tables.end());
    if (it != a.jump_tables.end())
        BS_CHECK(it->second.targets == std::vector<uint32_t>({0x8C01000C, 0x8C01000A}) && !it->second.bounded);
    BS_CHECK(a.functions.at(kBase).labels.count(0x8C01000A) && a.functions.at(kBase).unresolved.empty());
}

// Tabela cujos destinos ficam longe do jmp (ponteiros para outras funcoes) nao vira label.
BS_TEST(analysis_pool_jump_table_rejects_far_targets) {
    analysis::Image img = image({
        0xD303, 0x033E, 0x70FC, 0x432B, 0x0009, 0x0009, 0x0009, 0x000B,
        0x0014, 0x8C01,  // pool: tabela em 8C010014
        0x0000, 0x8C02,  // tabela[0] = 8C020000 (64 KB adiante)
    });
    img.bytes.resize(0x10004, 0);
    img.bytes[0x10000] = 0x0B;  // 8C020000: rts; nop
    img.bytes[0x10002] = 0x09;
    const auto a = analyze(img);
    BS_CHECK(!a.jump_tables.count(0x8C010006));
    BS_CHECK(a.functions.at(kBase).unresolved.count(0x8C010006));
}

// Vetores de excecao: codigo copiado para VBR+0x600 e executado la. Instrucoes sao
// analisadas no destino; o pool nao e constante (o jogo o corrige na copia), entao o
// jmp fica no despacho em tempo de execucao.
BS_TEST(analysis_relocated_code_runs_at_destination_with_mutable_pool) {
    analysis::Image img = image({
        0x000B, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,  // 8C010000 entrada
        0xD101,          // 8C010010 mov.l @(disp=1,pc),r1 -> pool em +8
        0x412B,          // 8C010012 jmp @r1
        0x0009,          // 8C010014 nop
        0x0009,          // 8C010016
        0x0020, 0x8C01,  // 8C010018 pool: 0x8C010020
        0x0009, 0x0009,  // 8C01001C
        0x000B, 0x0009,  // 8C010020 rts; nop
    });
    img.relocated.push_back({0x8C00FA00, 0x8C010010, 0x0C});
    BS_CHECK(img.read16(0x8C00FA02) == 0x412B && img.read32(0x8C00FA08) == 0x8C010020);
    BS_CHECK(img.normalize(0xAC00FA04) == 0x8C00FA04 && img.normalize(0x8C00FA0C) == 0);
    const auto a = analyze(img, {0x8C00FA00, 0x8C010020});
    BS_CHECK(a.functions.count(0x8C00FA00));
    const auto& f = a.functions.at(0x8C00FA00);
    BS_CHECK(f.unresolved.count(0x8C00FA02) && f.direct.empty());
    BS_CHECK(a.data.count(0x8C00FA08));  // pool no destino e dado
}

// Falso positivo real (8C130594, dado): "mac.l; sts.l mach,@-r4; bsr ...". Push que nao e
// na pilha (r15) nao e prologo; uma constante do pool apontando para la nao cria funcao.
BS_TEST(analysis_pointer_candidate_rejects_push_off_stack) {
    const auto img = image({
        0xD101,          // 8C010000 mov.l 0x8C010008,r1  (constante: 8C010010)
        0x000B,          // 8C010002 rts
        0x0009,          // 8C010004 nop
        0x0009,          // 8C010006
        0x0010, 0x8C01,  // 8C010008 pool: 0x8C010010
        0x0009, 0x0009,  // 8C01000C
        0x015F,          // 8C010010 mac.l @r5+,@r1+   (dado)
        0x4402,          // 8C010012 sts.l mach,@-r4
        0xBFF0,          // 8C010014 bsr (para tras)
        0x455A,          // 8C010016 lds r5,fpul
        0x000B, 0x0009,  // 8C010018 rts; nop
    });
    analysis::Config cfg;
    cfg.entry              = kBase;
    cfg.pointer_candidates = true;
    cfg.data_pointers      = false;
    const auto a           = analysis::analyze(img, cfg);
    BS_CHECK(!a.functions.count(0x8C010010));
    BS_CHECK(a.stats.dead_ends == 0);
}

// Regressao do NEW GAME (PC 8C1953E2 desviava para 8C194580, "rts; mov #0,r0", metodo de uma
// vtable em 8C1DBF58 que o analisador nao conhecia): numa tabela de ponteiros em dado com
// >= 3 entradas e >= 2 funcoes ja conhecidas, as outras entradas viram funcoes mesmo sendo
// folhas de 2 instrucoes. Sem a evidencia da tabela (1 conhecida), nada muda.
BS_TEST(analysis_pointer_table_finds_short_methods) {
    auto prog = [](bool twoKnown) {
        return image({
            0xB006, 0x0009,  // 8C010000 bsr F1 (8C010010); nop
            0xB006, 0x0009,  // 8C010004 bsr F2 (8C010014); nop
            0x000B, 0x0009,  // 8C010008 rts; nop
            0x0009, 0x0009,  // 8C01000C padding
            0x000B, 0xE001,  // 8C010010 F1: rts; mov #1,r0
            0x000B, 0xE002,  // 8C010014 F2: rts; mov #2,r0
            0x000B, 0xE000,  // 8C010018 S1: rts; mov #0,r0 (so na tabela)
            0x000B, 0xE005,  // 8C01001C S2: rts; mov #5,r0 (so na tabela)
            // 8C010020 tabela: F1, S1, F2|S2, S2
            0x0010, 0x8C01, 0x0018, 0x8C01, uint16_t(twoKnown ? 0x0014 : 0x001C), 0x8C01, 0x001C, 0x8C01,
            0xFFFF, 0xFFFF,  // fim da tabela
        });
    };
    analysis::Config cfg;
    cfg.entry              = kBase;
    cfg.pointer_candidates = false;
    cfg.data_pointers      = true;
    const auto a = analysis::analyze(prog(true), cfg);
    BS_CHECK(a.functions.count(0x8C010018) && a.functions.at(0x8C010018).origin == analysis::Origin::PointerTable);
    BS_CHECK(a.functions.count(0x8C01001C));
    BS_CHECK(a.stats.dead_ends == 0 && a.stats.orphans == 0);
    // so uma funcao conhecida na tabela: sem evidencia suficiente, as folhas curtas ficam de fora
    const auto b = analysis::analyze(prog(false), cfg);
    BS_CHECK(!b.functions.count(0x8C010018) && !b.functions.count(0x8C01001C));
}