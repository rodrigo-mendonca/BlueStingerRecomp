// Testes de semantica das instrucoes SH-4 (runtime/cpu/sh4_ops.h).
#include "test.h"

#include "cpu/sh4_ops.h"
#include "decoder/sh4_decoder.h"

#include <cfenv>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

using namespace bs;
using namespace bs::op;

namespace {

// Tabela completa Op -> funcao: falha de compilacao se alguma instrucao nao tiver semantica.
using OpFn = void (*)(Sh4Context*, uint32_t, uint32_t, uint32_t);
const OpFn kOpTable[] = {
    nullptr,
#define X(name, pat, mnem, ops, imm, flags) &bs::op::name,
#include "decoder/sh4_isa.inc"
#undef X
};
static_assert(std::size(kOpTable) == size_t(sh4::Op::Count));

struct Cpu {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    Sh4Context           c{};
    Cpu() {
        c.ram = ram.data();
        sh4_reset(c, 0x8C010000);
        sh4_set_sr(c, 0x40000000);   // MD=1, RB=0, BL=0
        sh4_set_fpscr(c, 0x00040001);
    }
    ~Cpu() { std::fesetround(FE_TONEAREST); }
    // Executa uma instrucao codificada (exceto desvios), como o codigo gerado faria.
    void exec(uint16_t opcode, uint32_t pc = 0x8C010000) {
        const sh4::Insn in = sh4::decode(opcode);
        uint32_t        imm = uint32_t(in.imm);
        if (in.info().imm == sh4::ImmKind::PC8x2 || in.info().imm == sh4::ImmKind::PC8x4) imm = sh4::target(in, pc);
        kOpTable[size_t(in.op)](&c, in.rn, in.rm, imm);
    }
    uint32_t& r(int i) { return c.r[i]; }
};

std::mt19937_64 rng(12345);
uint32_t        rnd32() { return uint32_t(rng()); }

} // namespace

BS_TEST(ops_addc_subc_negc_64bit_chains) {
    Cpu p;
    for (int k = 0; k < 2000; ++k) {
        const uint64_t a = rng(), b = rng();
        // 64-bit add: r1:r0 += r3:r2
        p.r(0) = uint32_t(a); p.r(1) = uint32_t(a >> 32); p.r(2) = uint32_t(b); p.r(3) = uint32_t(b >> 32);
        p.exec(0x0008);  // clrt
        p.exec(0x302E);  // addc r2,r0
        p.exec(0x313E);  // addc r3,r1
        BS_CHECK(((uint64_t(p.r(1)) << 32) | p.r(0)) == a + b);
        BS_CHECK(p.c.t == uint32_t((a + b) < a));
        // 64-bit sub
        p.r(0) = uint32_t(a); p.r(1) = uint32_t(a >> 32);
        p.exec(0x0008);
        p.exec(0x302A);  // subc r2,r0
        p.exec(0x313A);  // subc r3,r1
        BS_CHECK(((uint64_t(p.r(1)) << 32) | p.r(0)) == a - b);
        BS_CHECK(p.c.t == uint32_t(a < b));
        // 64-bit neg: r1:r0 = -(r3:r2)
        p.exec(0x0008);
        p.exec(0x602A);  // negc r2,r0
        p.exec(0x613A);  // negc r3,r1
        BS_CHECK(((uint64_t(p.r(1)) << 32) | p.r(0)) == 0 - b);
    }
}

BS_TEST(ops_addv_subv_overflow) {
    Cpu p;
    const int32_t cases[][2] = {{0x7FFFFFFF, 1}, {-0x7FFFFFFF - 1, -1}, {5, 7}, {-5, 7}, {0x40000000, 0x40000000}};
    for (auto& cs : cases) {
        const int64_t s = int64_t(cs[0]) + cs[1], d = int64_t(cs[0]) - cs[1];
        p.r(1) = uint32_t(cs[0]); p.r(2) = uint32_t(cs[1]);
        p.exec(0x312F);  // addv r2,r1
        BS_CHECK(p.c.t == uint32_t(s != int32_t(s)));
        BS_CHECK(p.r(1) == uint32_t(int32_t(s)));
        p.r(1) = uint32_t(cs[0]);
        p.exec(0x312B);  // subv r2,r1
        BS_CHECK(p.c.t == uint32_t(d != int32_t(d)));
    }
}

// Rotina do manual: 32 / 16 sem sinal. R1 = dividendo, R0 = divisor << 16 -> R1 = quociente.
BS_TEST(ops_div1_unsigned_32_by_16) {
    Cpu p;
    for (int k = 0; k < 3000; ++k) {
        const uint32_t dv = (rnd32() & 0xFFFF) | 1;
        const uint32_t dd = rnd32() % (dv << 16);  // quociente cabe em 16 bits
        p.r(1) = dd;
        p.r(0) = dv;
        p.exec(0x4028);  // shll16 r0
        p.exec(0x0019);  // div0u
        for (int s = 0; s < 16; ++s) p.exec(0x3104);  // div1 r0,r1
        p.exec(0x4124);  // rotcl r1
        p.exec(0x611D);  // extu.w r1,r1
        BS_CHECK(p.r(1) == dd / dv);
    }
}

// Rotina do manual: 64 / 32 sem sinal. R1:R2 / R0 -> R2 = quociente (exige R1 < R0).
BS_TEST(ops_div1_unsigned_64_by_32) {
    Cpu p;
    for (int k = 0; k < 3000; ++k) {
        const uint32_t dv = rnd32() | 1;
        const uint64_t dd = ((uint64_t(rnd32() % dv)) << 32) | rnd32();
        p.r(1) = uint32_t(dd >> 32);
        p.r(2) = uint32_t(dd);
        p.r(0) = dv;
        p.exec(0x0019);  // div0u
        for (int s = 0; s < 32; ++s) {
            p.exec(0x4224);  // rotcl r2
            p.exec(0x3104);  // div1 r0,r1
        }
        p.exec(0x4224);  // rotcl r2
        BS_CHECK(p.r(2) == uint32_t(dd / dv));
    }
}

// Rotina do manual: 32 / 32 com sinal. R1 / R0 -> R1 = quociente (truncado).
BS_TEST(ops_div1_signed_32_by_32) {
    Cpu p;
    int bad = 0;
    for (int k = 0; k < 5000; ++k) {
        int32_t dv = int32_t(rnd32());
        if (k % 3 == 0) dv = int32_t(rnd32() % 2001) - 1000;
        if (dv == 0) dv = 7;
        int32_t dd = int32_t(rnd32());
        if (dd == INT32_MIN && dv == -1) continue;
        p.r(1) = uint32_t(dd);
        p.r(0) = uint32_t(dv);
        p.exec(0x6213);  // mov r1,r2
        p.exec(0x4224);  // rotcl r2
        p.exec(0x322A);  // subc r2,r2
        p.exec(0x233A);  // xor r3,r3
        p.exec(0x313A);  // subc r3,r1
        p.exec(0x2207);  // div0s r0,r2
        for (int s = 0; s < 32; ++s) {
            p.exec(0x4124);  // rotcl r1
            p.exec(0x3204);  // div1 r0,r2
        }
        p.exec(0x4124);  // rotcl r1
        p.exec(0x313E);  // addc r3,r1
        if (int32_t(p.r(1)) != dd / dv && bad++ < 5)
            std::fprintf(stderr, "  %d / %d = %d (esperado %d)\n", dd, dv, int32_t(p.r(1)), dd / dv);
    }
    BS_CHECK(bad == 0);
}

BS_TEST(ops_mul_and_mac) {
    Cpu p;
    p.r(1) = 0xFFFFFFFE; p.r(2) = 3;
    p.exec(0x312D);  // dmuls.l r2,r1
    BS_CHECK(p.c.mach == 0xFFFFFFFF && p.c.macl == 0xFFFFFFFA);
    p.exec(0x3125);  // dmulu.l r2,r1
    BS_CHECK(p.c.mach == 2 && p.c.macl == 0xFFFFFFFA);
    p.r(1) = 0x0001FFFF; p.r(2) = 0x0000FFFF;
    p.exec(0x212F);  // muls.w r2,r1: (-1)*(-1)
    BS_CHECK(p.c.macl == 1);
    p.exec(0x212E);  // mulu.w r2,r1
    BS_CHECK(p.c.macl == 0xFFFFu * 0xFFFFu);

    // mac.l com saturacao (S=1) em 48 bits
    const uint32_t a = 0x8C100000;
    sh4_write32(&p.c, a, 0x7FFFFFFF);
    sh4_write32(&p.c, a + 4, 0x7FFFFFFF);
    p.exec(0x0028);  // clrmac
    p.exec(0x0058);  // sets
    p.r(1) = a; p.r(2) = a + 4;
    p.exec(0x021F);  // mac.l @r1+,@r2+
    BS_CHECK(p.c.mach == 0x00007FFF && p.c.macl == 0xFFFFFFFF);
    BS_CHECK(p.r(1) == a + 4 && p.r(2) == a + 8);
    // mac.l sem saturacao e com n == m: le @Rn, incrementa, le de novo
    p.exec(0x0048);  // clrs
    p.exec(0x0028);
    sh4_write32(&p.c, a, 3);
    sh4_write32(&p.c, a + 4, 5);
    p.r(1) = a;
    p.exec(0x011F);  // mac.l @r1+,@r1+
    BS_CHECK(p.c.macl == 15 && p.c.mach == 0 && p.r(1) == a + 8);

    // mac.w com S=1 satura em 32 bits e marca MACH bit 0
    sh4_write16(&p.c, a, 0x7FFF);
    sh4_write16(&p.c, a + 2, 0x7FFF);
    p.c.mach = 0; p.c.macl = 0x7FFFFFF0;
    p.exec(0x0058);  // sets
    p.r(1) = a; p.r(2) = a + 2;
    p.exec(0x412F);  // mac.w @r2+,@r1+
    BS_CHECK(p.c.macl == 0x7FFFFFFF && (p.c.mach & 1));
}

BS_TEST(ops_shifts) {
    Cpu p;
    struct { uint32_t v; int32_t s; uint32_t shad, shld; } cs[] = {
        {0x80000001, 4, 0x00000010, 0x00000010},
        {0x80000000, -4, 0xF8000000, 0x08000000},
        {0x80000000, -32, 0xFFFFFFFF, 0},  // (Rm & 31) == 0 com Rm negativo
        {0x40000000, -32, 0, 0},
        {0x12345678, 31, 0, 0},
        {0x12345678, 32, 0x12345678, 0x12345678},  // so os 5 bits baixos
        {0xFFFFFFFF, -1, 0xFFFFFFFF, 0x7FFFFFFF},
    };
    for (auto& k : cs) {
        p.r(1) = k.v; p.r(2) = uint32_t(k.s);
        p.exec(0x412C);  // shad r2,r1
        BS_CHECK(p.r(1) == k.shad);
        p.r(1) = k.v;
        p.exec(0x412D);  // shld r2,r1
        BS_CHECK(p.r(1) == k.shld);
    }
    p.r(1) = 0x80000000; p.c.t = 1;
    p.exec(0x4124);  // rotcl r1
    BS_CHECK(p.r(1) == 1 && p.c.t == 1);
    p.exec(0x4125);  // rotcr r1 (T=1 entra no topo, sai bit 0 = 1)
    BS_CHECK(p.r(1) == 0x80000000 && p.c.t == 1);
    p.r(1) = 0x80000001;
    p.exec(0x4121);  // shar r1
    BS_CHECK(p.r(1) == 0xC0000000 && p.c.t == 1);
}

BS_TEST(ops_compare_and_misc) {
    Cpu p;
    p.r(1) = 0x12345678; p.r(2) = 0xAB34CDEF;
    p.exec(0x212C);  // cmp/str r2,r1 -> byte 0x34 igual
    BS_CHECK(p.c.t == 1);
    p.r(2) = 0xAAAAAAAA;
    p.exec(0x212C);
    BS_CHECK(p.c.t == 0);
    p.r(1) = 0xFFFFFFFF; p.r(2) = 1;
    p.exec(0x3126);  // cmp/hi r2,r1 (sem sinal)
    BS_CHECK(p.c.t == 1);
    p.exec(0x3127);  // cmp/gt r2,r1 (com sinal: -1 > 1?)
    BS_CHECK(p.c.t == 0);
    p.r(0) = 0xFFFFFFFF;
    p.exec(0x88FF);  // cmp/eq #-1,r0
    BS_CHECK(p.c.t == 1);
    p.r(1) = 0x11223344; p.r(2) = 0xAABBCCDD;
    p.exec(0x612D + 0x0000);  // extu.w r2,r1
    BS_CHECK(p.r(1) == 0xCCDD);
    p.r(1) = 0x11223344;
    p.exec(0x212D);  // xtrct r2,r1
    BS_CHECK(p.r(1) == 0xCCDD1122);
    p.exec(0x6128);  // swap.b r2,r1
    BS_CHECK(p.r(1) == 0xAABBDDCC);
    p.exec(0x6129);  // swap.w r2,r1
    BS_CHECK(p.r(1) == 0xCCDDAABB);
    p.r(1) = 1;
    p.exec(0x4110);  // dt r1
    BS_CHECK(p.r(1) == 0 && p.c.t == 1);
}

BS_TEST(ops_memory_addressing_modes) {
    Cpu p;
    const uint32_t a = 0x8C200000;
    // @-Rn com n == m grava o valor original
    p.r(1) = a;
    p.exec(0x2116);  // mov.l r1,@-r1
    BS_CHECK(p.r(1) == a - 4 && sh4_read32(&p.c, a - 4) == a);
    // @Rm+ com n == m: valor lido prevalece
    sh4_write32(&p.c, a, 0xDEADBEEF);
    p.r(1) = a;
    p.exec(0x6116);  // mov.l @r1+,r1
    BS_CHECK(p.r(1) == 0xDEADBEEF);
    // leituras de byte/word estendem sinal
    sh4_write8(&p.c, a, 0x80);
    p.r(2) = a;
    p.exec(0x6120);  // mov.b @r2,r1
    BS_CHECK(p.r(1) == 0xFFFFFF80);
    // espelhos: P2 (0xAC...) e P0 (0x0C...) enxergam a mesma RAM
    BS_CHECK(sh4_read8(&p.c, 0xAC200000) == 0x80 && sh4_read8(&p.c, 0x0C200000) == 0x80);
    // GBR e TAS.B
    p.c.gbr = a; p.r(0) = 8;
    p.exec(0xCF01);  // or.b #1,@(r0,gbr)
    BS_CHECK(sh4_read8(&p.c, a + 8) == 1);
    p.r(3) = a + 16;
    p.exec(0x431B);  // tas.b @r3
    BS_CHECK(p.c.t == 1 && sh4_read8(&p.c, a + 16) == 0x80);
    // mov.l PC-relativo (alvo calculado pelo gerador: (PC & ~3) + 4 + disp*4)
    sh4_write32(&p.c, 0x8C010018, 0xCAFEBABE);
    p.exec(0xD105, 0x8C010002);  // mov.l @(20,PC),r1
    BS_CHECK(p.r(1) == 0xCAFEBABE);
    sh4_write16(&p.c, 0x8C01000A, 0x8001);
    p.exec(0x9102, 0x8C010002);  // mov.w @(4,PC),r1
    BS_CHECK(p.r(1) == 0xFFFF8001);
}

BS_TEST(ops_register_banks_and_sr) {
    Cpu p;
    p.r(0) = 111;
    p.c.rbank[0] = 222;
    p.r(1) = 0x60000000;  // MD=1, RB=1
    p.exec(0x410E);  // ldc r1,sr
    BS_CHECK(p.r(0) == 222 && p.c.rbank[0] == 111);
    p.exec(0x0182);  // stc r0_bank,r1
    BS_CHECK(p.r(1) == 111);
    p.r(2) = 0x700083F3 | 0x0000FC00;  // bits nao gravaveis sao descartados
    p.exec(0x420E);  // ldc r2,sr
    BS_CHECK(sh4_get_sr(p.c) == 0x700083F3);
    BS_CHECK(p.c.t == 1);
}

BS_TEST(ops_fpu_single_double_and_modes) {
    Cpu p;
    p.c.fr[1] = u32(1.5f); p.c.fr[2] = u32(2.25f);
    p.exec(0xF120);  // fadd fr2,fr1
    BS_CHECK(f32(p.c.fr[1]) == 3.75f);

    // RM=01 (para zero, padrao do Dreamcast) vs RM=00: 1/3 difere no ultimo bit
    p.c.fr[1] = u32(1.0f); p.c.fr[2] = u32(3.0f);
    p.exec(0xF123);  // fdiv fr2,fr1
    BS_CHECK(p.c.fr[1] == 0x3EAAAAAAu);
    sh4_set_fpscr(p.c, 0x00040000);
    p.c.fr[1] = u32(1.0f);
    p.exec(0xF123);
    BS_CHECK(p.c.fr[1] == 0x3EAAAAABu);

    // PR=1: dupla precisao em pares (FRn = metade alta)
    sh4_set_fpscr(p.c, kFpscrPR | kFpscrDN);
    setDR(&p.c, 2, 1.25);
    setDR(&p.c, 4, 2.5);
    p.exec(0xF240);  // fadd dr4,dr2
    BS_CHECK(getDR(&p.c, 2) == 3.75);
    BS_CHECK(p.c.fr[2] == 0x400E0000u && p.c.fr[3] == 0);
    p.c.fpul = uint32_t(-7);
    p.exec(0xF62D);  // float fpul,dr6
    BS_CHECK(getDR(&p.c, 6) == -7.0);

    // FTRC satura e trata NaN
    sh4_set_fpscr(p.c, 0x00040001);
    p.c.fr[3] = u32(3e10f);
    p.exec(0xF33D);  // ftrc fr3,fpul
    BS_CHECK(p.c.fpul == 0x7FFFFFFFu);
    p.c.fr[3] = 0x7FC00000u;
    p.exec(0xF33D);
    BS_CHECK(p.c.fpul == 0x80000000u);
    p.c.fr[3] = u32(-2.9f);
    p.exec(0xF33D);
    BS_CHECK(p.c.fpul == uint32_t(-2));
    // fcmp com NaN -> T=0
    p.c.fr[4] = 0x7FC00000u; p.c.fr[5] = u32(1.0f);
    p.exec(0xF454);  // fcmp/eq fr5,fr4
    BS_CHECK(p.c.t == 0);
}

BS_TEST(ops_fpu_banks_and_64bit_moves) {
    Cpu p;
    p.c.fr[0] = 1; p.c.xf[0] = 2;
    p.exec(0xFBFD);  // frchg
    BS_CHECK(p.c.fr[0] == 2 && p.c.xf[0] == 1 && (p.c.fpscr & kFpscrFR));
    p.exec(0xFBFD);
    BS_CHECK(p.c.fr[0] == 1);

    p.exec(0xF3FD);  // fschg -> SZ=1
    BS_CHECK(p.c.fpscr & kFpscrSZ);
    const uint32_t a = 0x8C300000;
    sh4_write32(&p.c, a, 0x11111111);
    sh4_write32(&p.c, a + 4, 0x22222222);
    p.r(1) = a;
    p.exec(0xF319);  // fmov @r1+,xd2 (indice impar = XD)
    BS_CHECK(p.c.xf[2] == 0x11111111 && p.c.xf[3] == 0x22222222 && p.r(1) == a + 8);
    p.exec(0xF43C);  // fmov xd2,dr4
    BS_CHECK(p.c.fr[4] == 0x11111111 && p.c.fr[5] == 0x22222222);
    p.r(2) = a + 64;
    p.exec(0xF24B);  // fmov dr4,@-r2
    BS_CHECK(p.r(2) == a + 56 && sh4_read32(&p.c, a + 56) == 0x11111111 && sh4_read32(&p.c, a + 60) == 0x22222222);
    p.exec(0xF3FD);  // fschg -> SZ=0
}

BS_TEST(ops_fpu_vector_and_trig) {
    Cpu p;
    // FIPR fv4,fv0: FR3 = FV0 . FV4
    for (int k = 0; k < 4; ++k) { p.c.fr[k] = u32(float(k + 1)); p.c.fr[4 + k] = u32(float(10 * (k + 1))); }
    p.exec(0xF1ED);  // fipr fv4,fv0 (nn=00, mm=01)
    BS_CHECK(f32(p.c.fr[3]) == 300.0f);

    // FTRV: XMTRX por colunas; matriz de translacao (1,2,3) aplicada a (1,1,1,1)
    for (int k = 0; k < 16; ++k) p.c.xf[k] = u32((k % 5 == 0) ? 1.0f : 0.0f);
    p.c.xf[12] = u32(1.0f); p.c.xf[13] = u32(2.0f); p.c.xf[14] = u32(3.0f);
    for (int k = 0; k < 4; ++k) p.c.fr[8 + k] = u32(1.0f);
    p.exec(0xF9FD);  // ftrv xmtrx,fv8
    BS_CHECK(f32(p.c.fr[8]) == 2.0f && f32(p.c.fr[9]) == 3.0f && f32(p.c.fr[10]) == 4.0f && f32(p.c.fr[11]) == 1.0f);

    // FSCA: 0x4000 = 90 graus
    p.c.fpul = 0x4000;
    p.exec(0xF2FD);  // fsca fpul,dr2
    BS_CHECK(f32(p.c.fr[2]) == 1.0f && std::fabs(f32(p.c.fr[3])) < 1e-6f);
    p.c.fpul = 0x18000;  // so os 16 bits baixos: 0x8000 = 180 graus
    p.exec(0xF2FD);
    BS_CHECK(std::fabs(f32(p.c.fr[2])) < 1e-6f && f32(p.c.fr[3]) == -1.0f);

    // FSRRA
    p.c.fr[5] = u32(4.0f);
    p.exec(0xF57D);
    BS_CHECK(f32(p.c.fr[5]) == 0.5f);
}

// Revisao Codex M3: em modo usuario (MD=0) R0-R7 sao sempre BANK0, mesmo com RB=1.
BS_TEST(ops_register_bank_depends_on_md_and_rb) {
    Cpu p;  // estado: MD=1, RB=0 -> BANK0 ativo
    p.r(0) = 0xB0;          // BANK0.R0
    p.c.rbank[0] = 0xB1;    // BANK1.R0
    sh4_set_sr(p.c, 0x60000000);  // MD=1 RB=1 -> BANK1
    BS_CHECK(p.r(0) == 0xB1);
    sh4_set_sr(p.c, 0x20000000);  // MD=0 RB=1 -> modo usuario: BANK0
    BS_CHECK(p.r(0) == 0xB0 && p.c.rbank[0] == 0xB1);
    sh4_set_sr(p.c, 0x60000000);  // volta a MD=1 RB=1 -> BANK1
    BS_CHECK(p.r(0) == 0xB1);
    sh4_set_sr(p.c, 0x40000000);  // MD=1 RB=0 -> BANK0
    BS_CHECK(p.r(0) == 0xB0);
}

// Revisao Codex M3: FPSCR.DN=1 trata denormalizados como zero (entrada e resultado).
BS_TEST(ops_fpscr_dn_flushes_denormals) {
    Cpu p;  // FPSCR de reset: DN=1
    p.c.fr[1] = 0x00000001u; p.c.fr[2] = 0x00000001u;  // menor denormal positivo
    p.exec(0xF120);  // fadd fr2,fr1
    BS_CHECK(p.c.fr[1] == 0);
    sh4_set_fpscr(p.c, 0x00000001);  // DN=0, RM=01
    p.c.fr[1] = 0x00000001u;
    p.exec(0xF120);
    BS_CHECK(p.c.fr[1] == 0x00000002u);
    sh4_set_fpscr(p.c, 0x00040001);
}
