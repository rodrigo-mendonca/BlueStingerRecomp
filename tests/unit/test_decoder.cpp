#include "test.h"

#include "decoder/sh4_decoder.h"

#include <cstring>
#include <map>
#include <string>

using namespace bs::sh4;

BS_TEST(decoder_table_has_no_overlapping_patterns) {
    const auto c = tableConflicts();
    for (const auto& s : c) std::fprintf(stderr, "  conflito: %s\n", s.c_str());
    BS_CHECK(c.empty());
}

// Cada instrucao precisa ser alcancada por exatamente 2^(bits livres) opcodes:
// nenhuma e sombreada por outra nem fica sem opcode.
BS_TEST(decoder_every_op_reachable_with_expected_count) {
    std::map<Op, uint32_t> count;
    for (uint32_t op = 0; op < 65536; ++op) ++count[decode(uint16_t(op)).op];
    for (uint16_t k = 1; k < uint16_t(Op::Count); ++k) {
        const OpInfo& inf  = opInfo(Op(k));
        uint32_t      free = 0;
        for (const char* p = inf.pattern; *p; ++p) free += (*p != '0' && *p != '1');
        if (count[Op(k)] != (1u << free)) {
            std::fprintf(stderr, "  %s: %u opcodes, esperado %u\n", inf.name, count[Op(k)], 1u << free);
            BS_CHECK(false);
        }
    }
}

namespace {
struct Vec {
    uint16_t    op;
    uint32_t    pc;
    const char* text;
};
// Vetores conferidos contra o Renesas SH-4 Software Manual e o Capstone 6.0.0.
constexpr uint32_t kPc = 0x8C010000;
const Vec          kVectors[] = {
    {0xE3FF, kPc, "mov #-1,r3"},
    {0xE07F, kPc, "mov #127,r0"},
    {0xD105, kPc, "mov.l 0x8c010018,r1"},
    {0xD105, kPc + 2, "mov.l 0x8c010018,r1"},  // (PC & ~3) + 4 + disp*4
    {0x9102, kPc, "mov.w 0x8c010008,r1"},
    {0x9102, kPc + 2, "mov.w 0x8c01000a,r1"},  // PC + 4 + disp*2
    {0xC701, kPc, "mova 0x8c010008,r0"},
    {0x6543, kPc, "mov r4,r5"},
    {0x2F46, kPc, "mov.l r4,@-r15"},
    {0x64F6, kPc, "mov.l @r15+,r4"},
    {0x5452, kPc, "mov.l @(8,r5),r4"},
    {0x1F51, kPc, "mov.l r5,@(4,r15)"},
    {0x8401, kPc, "mov.b @(1,r0),r0"},
    {0x8512, kPc, "mov.w @(4,r1),r0"},
    {0x8123, kPc, "mov.w r0,@(6,r2)"},
    {0xC6FF, kPc, "mov.l @(1020,gbr),r0"},
    {0x034C, kPc, "mov.b @(r0,r4),r3"},
    {0x0029, kPc, "movt r0"},
    {0x7FFC, kPc, "add #-4,r15"},
    {0x88FF, kPc, "cmp/eq #-1,r0"},
    {0xC9FF, kPc, "and #255,r0"},
    {0xCDFF, kPc, "and.b #255,@(r0,gbr)"},
    {0x3127, kPc, "cmp/gt r2,r1"},
    {0x4110, kPc, "dt r1"},
    {0x0FFF, kPc, "mac.l @r15+,@r15+"},
    {0x412C, kPc, "shad r2,r1"},
    {0x8F10, kPc, "bf/s 0x8c010024"},
    {0x89FE, kPc, "bt 0x8c010000"},
    {0x8B80, kPc, "bf 0x8c00ff04"},
    {0xA800, kPc, "bra 0x8c00f004"},
    {0xAFFE, kPc, "bra 0x8c010000"},
    {0xB7FF, kPc, "bsr 0x8c011002"},
    {0x0123, kPc, "braf r1"},
    {0x0203, kPc, "bsrf r2"},
    {0x412B, kPc, "jmp @r1"},
    {0x410B, kPc, "jsr @r1"},
    {0x000B, kPc, "rts"},
    {0x002B, kPc, "rte"},
    {0x0009, kPc, "nop"},
    {0x4F22, kPc, "sts.l pr,@-r15"},
    {0x4F26, kPc, "lds.l @r15+,pr"},
    {0x418E, kPc, "ldc r1,r0_bank"},
    {0x41F7, kPc, "ldc.l @r1+,r7_bank"},
    {0x0182, kPc, "stc r0_bank,r1"},
    {0x4F32, kPc, "stc.l sgr,@-r15"},
    {0x00FA, kPc, "stc dbr,r0"},
    {0x401E, kPc, "ldc r0,gbr"},
    {0xC305, kPc, "trapa #5"},
    {0x0093, kPc, "ocbi @r0"},
    {0x01C3, kPc, "movca.l r0,@r1"},
    {0xF00C, kPc, "fmov fr0,fr0"},
    {0xF3A8, kPc, "fmov.s @r10,fr3"},
    {0xFF4B, kPc, "fmov.s fr4,@-r15"},
    {0xF12E, kPc, "fmac fr0,fr2,fr1"},
    {0xF18D, kPc, "fldi0 fr1"},
    {0xF21D, kPc, "flds fr2,fpul"},
    {0xF32D, kPc, "float fpul,fr3"},
    {0xF43D, kPc, "ftrc fr4,fpul"},
    {0xF06D, kPc, "fsqrt fr0"},
    {0xF77D, kPc, "fsrra fr7"},
    {0xF2AD, kPc, "fcnvsd fpul,dr2"},
    {0xF4BD, kPc, "fcnvds dr4,fpul"},
    {0xF5ED, kPc, "fipr fv4,fv4"},
    {0xF9ED, kPc, "fipr fv4,fv8"},  // nn=10 (FVn), mm=01 (FVm)
    {0xF8ED, kPc, "fipr fv0,fv8"},
    {0xF1FD, kPc, "ftrv xmtrx,fv0"},
    {0xFDFD, kPc, "ftrv xmtrx,fv12"},
    {0xF2FD, kPc, "fsca fpul,dr2"},
    {0xFBFD, kPc, "frchg"},
    {0xF3FD, kPc, "fschg"},
    {0x406A, kPc, "lds r0,fpscr"},
    {0x0F5A, kPc, "sts fpul,r15"},
    {0xFFFF, kPc, ".word 0xffff"},
    {0x4036, kPc, ".word 0x4036"},  // ldc.l @R0+,SGR: SH-4A, invalido no SH-4
    {0x00AB, kPc, ".word 0x00ab"},  // synco: SH-4A
};
} // namespace

BS_TEST(decoder_disassembly_vectors) {
    for (const auto& v : kVectors) {
        const std::string got = disasm(decode(v.op), v.pc);
        if (got != v.text) {
            std::fprintf(stderr, "  %04x @%08x: '%s' esperado '%s'\n", v.op, v.pc, got.c_str(), v.text);
            BS_CHECK(false);
        }
    }
}

BS_TEST(decoder_fields_and_flags) {
    Insn i = decode(0x5452);  // mov.l @(8,r5),r4
    BS_CHECK(i.op == Op::MOVL_LD_DISP && i.rn == 4 && i.rm == 5 && i.imm == 8);

    i = decode(0xE380);  // mov #-128,r3
    BS_CHECK(i.op == Op::MOV_IMM && i.imm == -128);

    i = decode(0xC980);  // and #128,r0 (sem sinal)
    BS_CHECK(i.imm == 128);

    i = decode(0x8F10);  // bf/s
    BS_CHECK(i.has(F_BRANCH) && i.has(F_COND) && i.has(F_DELAYED) && !i.has(F_CALL));
    BS_CHECK(target(i, kPc) == kPc + 4 + 0x20);

    i = decode(0x8B00);  // bf (sem delay slot)
    BS_CHECK(i.has(F_COND) && !i.has(F_DELAYED));

    i = decode(0x410B);  // jsr @r1
    BS_CHECK(i.has(F_CALL) && i.has(F_INDIRECT) && i.has(F_DELAYED) && i.rn == 1);

    i = decode(0x000B);  // rts
    BS_CHECK(i.has(F_RETURN) && i.has(F_DELAYED) && i.has(F_SLOT_ILLEGAL));

    i = decode(0xC305);  // trapa: ilegal em slot, mas nao e desvio com slot
    BS_CHECK(i.has(F_SLOT_ILLEGAL) && !i.has(F_DELAYED));

    i = decode(0x0009);
    BS_CHECK(i.op == Op::NOP && !i.has(F_SLOT_ILLEGAL));

    i = decode(0xD105);
    BS_CHECK(i.has(F_PCREL) && target(i, kPc) == 0x8C010018);

    i = decode(0x400E);  // ldc r0,sr
    BS_CHECK(i.has(F_PRIV));
    BS_CHECK(!decode(0x401E).has(F_PRIV));  // ldc r0,gbr

    i = decode(0xF6ED);  // fipr fv8,fv4: campos de 2 bits
    BS_CHECK(i.op == Op::FIPR && i.rn == 1 && i.rm == 2 && i.has(F_FPU));

    i = decode(0x41F7);  // ldc.l @r1+,r7_bank: banco de 3 bits em rn
    BS_CHECK(i.op == Op::LDCL_BANK && i.rm == 1 && i.rn == 7);

    i = decode(0xB800);  // bsr com deslocamento minimo (-4096 bytes)
    BS_CHECK(i.imm == -4096 && target(i, kPc) == kPc + 4 - 4096);
}

BS_TEST(decoder_invalid_words) {
    BS_CHECK(!decode(0xFFFF).valid());
    BS_CHECK(!decode(0x0000).valid());
    BS_CHECK(!decode(0xF7FD).valid());  // fpchg (SH-4A)
    BS_CHECK(!decode(0xF1AD).valid());  // fcnvsd com bit 8 = 1
}
