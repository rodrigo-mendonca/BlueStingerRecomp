#pragma once
// Semantica de cada instrucao SH-4 (Renesas SH-4 Software Manual), uma funcao
// inline por instrucao com assinatura uniforme:
//
//     void NOME(Sh4Context* c, uint32_t n, uint32_t m, uint32_t i)
//
// n/m = campos do decodificador (Insn::rn/rm), i = Insn::imm (complemento de 2).
// Para MOVW_PC/MOVL_PC/MOVA, `i` e o endereco ja calculado pelo gerador.
// O codigo gerado chama estas funcoes com constantes e o compilador as inlina;
// os testes de semantica chamam as mesmas funcoes.
//
// Desvios (bra, bsr, jmp, rts, ...) sao emitidos pelo gerador e nao estao aqui;
// as entradas correspondentes em kOpTable apontam para `control_flow`.
#include "cpu/sh4_context.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace bs::op {

using C = Sh4Context;

#define BS_OP(name) \
    inline void name([[maybe_unused]] C* c, [[maybe_unused]] uint32_t n, [[maybe_unused]] uint32_t m, [[maybe_unused]] uint32_t i)

// ---------- auxiliares ----------
inline float    f32(uint32_t u) { return std::bit_cast<float>(u); }
inline uint32_t u32(float f) { return std::bit_cast<uint32_t>(f); }
inline double   getDR(const C* c, uint32_t n) {
    return std::bit_cast<double>((uint64_t(c->fr[n & 14]) << 32) | c->fr[(n & 14) + 1]);
}
inline void setDR(C* c, uint32_t n, double d) {
    const uint64_t v   = std::bit_cast<uint64_t>(d);
    c->fr[n & 14]      = uint32_t(v >> 32);
    c->fr[(n & 14) + 1] = uint32_t(v);
}
inline bool     pr(const C* c) { return (c->fpscr & kFpscrPR) != 0; }
inline bool     sz(const C* c) { return (c->fpscr & kFpscrSZ) != 0; }
inline uint32_t sext8(uint32_t v) { return uint32_t(int32_t(int8_t(v))); }
inline uint32_t sext16(uint32_t v) { return uint32_t(int32_t(int16_t(v))); }
inline bool     srbit(const C* c, uint32_t b) { return (c->sr & b) != 0; }
inline void     setsrbit(C* c, uint32_t b, bool v) { c->sr = v ? (c->sr | b) : (c->sr & ~b); }
// Par de registros de 64 bits para FMOV com SZ=1: indice par = DR (FR), impar = XD (XF).
inline uint32_t* pair(C* c, uint32_t r) { return (r & 1) ? &c->xf[r & 14] : &c->fr[r & 14]; }

inline uint32_t ftrc_sat(double v) {
    if (std::isnan(v)) return 0x80000000u;
    if (v >= 2147483648.0) return 0x7FFFFFFFu;
    if (v < -2147483648.0) return 0x80000000u;
    return uint32_t(int32_t(v));
}

BS_OP(control_flow) { sh4_illegal(c, c->pc, 0); }  // nunca emitido: o gerador trata desvios

// ---------- transferencia de dados ----------
BS_OP(MOV_IMM) { c->r[n] = i; }
BS_OP(MOVW_PC) { c->r[n] = sext16(sh4_read16(c, i)); }
BS_OP(MOVL_PC) { c->r[n] = sh4_read32(c, i); }
BS_OP(MOV) { c->r[n] = c->r[m]; }
BS_OP(MOVB_ST) { sh4_write8(c, c->r[n], c->r[m]); }
BS_OP(MOVW_ST) { sh4_write16(c, c->r[n], c->r[m]); }
BS_OP(MOVL_ST) { sh4_write32(c, c->r[n], c->r[m]); }
BS_OP(MOVB_LD) { c->r[n] = sext8(sh4_read8(c, c->r[m])); }
BS_OP(MOVW_LD) { c->r[n] = sext16(sh4_read16(c, c->r[m])); }
BS_OP(MOVL_LD) { c->r[n] = sh4_read32(c, c->r[m]); }
// @-Rn: o valor gravado e o Rm original, mesmo com n == m.
BS_OP(MOVB_PREDEC) { const uint32_t a = c->r[n] - 1; sh4_write8(c, a, c->r[m]); c->r[n] = a; }
BS_OP(MOVW_PREDEC) { const uint32_t a = c->r[n] - 2; sh4_write16(c, a, c->r[m]); c->r[n] = a; }
BS_OP(MOVL_PREDEC) { const uint32_t a = c->r[n] - 4; sh4_write32(c, a, c->r[m]); c->r[n] = a; }
// @Rm+: com n == m o valor lido prevalece (sem incremento).
BS_OP(MOVB_POSTINC) { const uint32_t v = sext8(sh4_read8(c, c->r[m])); if (n != m) c->r[m] += 1; c->r[n] = v; }
BS_OP(MOVW_POSTINC) { const uint32_t v = sext16(sh4_read16(c, c->r[m])); if (n != m) c->r[m] += 2; c->r[n] = v; }
BS_OP(MOVL_POSTINC) { const uint32_t v = sh4_read32(c, c->r[m]); if (n != m) c->r[m] += 4; c->r[n] = v; }
BS_OP(MOVB_ST_DISP) { sh4_write8(c, c->r[n] + i, c->r[0]); }
BS_OP(MOVW_ST_DISP) { sh4_write16(c, c->r[n] + i, c->r[0]); }
BS_OP(MOVL_ST_DISP) { sh4_write32(c, c->r[n] + i, c->r[m]); }
BS_OP(MOVB_LD_DISP) { c->r[0] = sext8(sh4_read8(c, c->r[m] + i)); }
BS_OP(MOVW_LD_DISP) { c->r[0] = sext16(sh4_read16(c, c->r[m] + i)); }
BS_OP(MOVL_LD_DISP) { c->r[n] = sh4_read32(c, c->r[m] + i); }
BS_OP(MOVB_ST_R0) { sh4_write8(c, c->r[n] + c->r[0], c->r[m]); }
BS_OP(MOVW_ST_R0) { sh4_write16(c, c->r[n] + c->r[0], c->r[m]); }
BS_OP(MOVL_ST_R0) { sh4_write32(c, c->r[n] + c->r[0], c->r[m]); }
BS_OP(MOVB_LD_R0) { c->r[n] = sext8(sh4_read8(c, c->r[m] + c->r[0])); }
BS_OP(MOVW_LD_R0) { c->r[n] = sext16(sh4_read16(c, c->r[m] + c->r[0])); }
BS_OP(MOVL_LD_R0) { c->r[n] = sh4_read32(c, c->r[m] + c->r[0]); }
BS_OP(MOVB_ST_GBR) { sh4_write8(c, c->gbr + i, c->r[0]); }
BS_OP(MOVW_ST_GBR) { sh4_write16(c, c->gbr + i, c->r[0]); }
BS_OP(MOVL_ST_GBR) { sh4_write32(c, c->gbr + i, c->r[0]); }
BS_OP(MOVB_LD_GBR) { c->r[0] = sext8(sh4_read8(c, c->gbr + i)); }
BS_OP(MOVW_LD_GBR) { c->r[0] = sext16(sh4_read16(c, c->gbr + i)); }
BS_OP(MOVL_LD_GBR) { c->r[0] = sh4_read32(c, c->gbr + i); }
BS_OP(MOVA) { c->r[0] = i; }
BS_OP(MOVT) { c->r[n] = c->t; }
BS_OP(MOVCA) { sh4_write32(c, c->r[n], c->r[0]); }
BS_OP(SWAPB) { const uint32_t v = c->r[m]; c->r[n] = (v & 0xFFFF0000u) | ((v & 0xFF) << 8) | ((v >> 8) & 0xFF); }
BS_OP(SWAPW) { const uint32_t v = c->r[m]; c->r[n] = (v << 16) | (v >> 16); }
BS_OP(XTRCT) { c->r[n] = (c->r[m] << 16) | (c->r[n] >> 16); }

// ---------- aritmetica ----------
BS_OP(ADD) { c->r[n] += c->r[m]; }
BS_OP(ADD_IMM) { c->r[n] += i; }
BS_OP(ADDC) {
    const uint32_t a = c->r[n], s = a + c->r[m], r = s + c->t;
    c->t = (a > s) || (s > r);
    c->r[n] = r;
}
BS_OP(ADDV) {
    const uint32_t a = c->r[n], b = c->r[m], r = a + b;
    c->t = (~(a ^ b) & (a ^ r)) >> 31;
    c->r[n] = r;
}
BS_OP(CMPEQ_IMM) { c->t = c->r[0] == i; }
BS_OP(CMPEQ) { c->t = c->r[n] == c->r[m]; }
BS_OP(CMPHS) { c->t = c->r[n] >= c->r[m]; }
BS_OP(CMPGE) { c->t = int32_t(c->r[n]) >= int32_t(c->r[m]); }
BS_OP(CMPHI) { c->t = c->r[n] > c->r[m]; }
BS_OP(CMPGT) { c->t = int32_t(c->r[n]) > int32_t(c->r[m]); }
BS_OP(CMPPZ) { c->t = int32_t(c->r[n]) >= 0; }
BS_OP(CMPPL) { c->t = int32_t(c->r[n]) > 0; }
BS_OP(CMPSTR) {
    const uint32_t x = c->r[n] ^ c->r[m];
    c->t = !(x & 0xFF000000u) || !(x & 0x00FF0000u) || !(x & 0x0000FF00u) || !(x & 0x000000FFu);
}
// DIV1: transcricao direta do algoritmo do manual (switch old_q / M / Q).
BS_OP(DIV1) {
    const bool     old_q = srbit(c, kSrQ);
    const bool     M     = srbit(c, kSrM);
    bool           Q     = (c->r[n] & 0x80000000u) != 0;
    const uint32_t tmp2  = c->r[m];
    uint32_t       rn    = (c->r[n] << 1) | c->t;
    const uint32_t tmp0  = rn;
    bool           tmp1;
    if (!old_q) {
        if (!M) { rn -= tmp2; tmp1 = rn > tmp0; Q = Q ? !tmp1 : tmp1; }
        else    { rn += tmp2; tmp1 = rn < tmp0; Q = Q ? tmp1 : !tmp1; }
    } else {
        if (!M) { rn += tmp2; tmp1 = rn < tmp0; Q = Q ? !tmp1 : tmp1; }
        else    { rn -= tmp2; tmp1 = rn > tmp0; Q = Q ? tmp1 : !tmp1; }
    }
    setsrbit(c, kSrQ, Q);
    c->t    = Q == M;
    c->r[n] = rn;
}
BS_OP(DIV0S) {
    const bool q = (c->r[n] >> 31) != 0, mm = (c->r[m] >> 31) != 0;
    setsrbit(c, kSrQ, q);
    setsrbit(c, kSrM, mm);
    c->t = q != mm;
}
BS_OP(DIV0U) { setsrbit(c, kSrQ, false); setsrbit(c, kSrM, false); c->t = 0; }
BS_OP(DMULS) {
    const int64_t v = int64_t(int32_t(c->r[n])) * int64_t(int32_t(c->r[m]));
    c->mach = uint32_t(uint64_t(v) >> 32);
    c->macl = uint32_t(v);
}
BS_OP(DMULU) {
    const uint64_t v = uint64_t(c->r[n]) * uint64_t(c->r[m]);
    c->mach = uint32_t(v >> 32);
    c->macl = uint32_t(v);
}
BS_OP(DT) { c->r[n] -= 1; c->t = c->r[n] == 0; }
BS_OP(EXTSB) { c->r[n] = sext8(c->r[m]); }
BS_OP(EXTSW) { c->r[n] = sext16(c->r[m]); }
BS_OP(EXTUB) { c->r[n] = c->r[m] & 0xFF; }
BS_OP(EXTUW) { c->r[n] = c->r[m] & 0xFFFF; }
// MAC: le @Rn, incrementa Rn, depois le @Rm (ordem do manual; importa com n == m).
BS_OP(MACL) {
    const int32_t a = int32_t(sh4_read32(c, c->r[n]));
    c->r[n] += 4;
    const int32_t b = int32_t(sh4_read32(c, c->r[m]));
    c->r[m] += 4;
    int64_t acc = int64_t((uint64_t(c->mach) << 32) | c->macl) + int64_t(a) * int64_t(b);
    if (srbit(c, kSrS)) {  // saturacao em 48 bits
        constexpr int64_t kMax = 0x00007FFFFFFFFFFFll, kMin = -0x0000800000000000ll;
        if (acc > kMax) acc = kMax;
        if (acc < kMin) acc = kMin;
    }
    c->mach = uint32_t(uint64_t(acc) >> 32);
    c->macl = uint32_t(acc);
}
BS_OP(MACW) {
    const int16_t a = int16_t(sh4_read16(c, c->r[n]));
    c->r[n] += 2;
    const int16_t b = int16_t(sh4_read16(c, c->r[m]));
    c->r[m] += 2;
    const int64_t prod = int64_t(a) * int64_t(b);
    if (srbit(c, kSrS)) {  // saturacao em 32 bits sobre MACL; MACH bit 0 sinaliza overflow
        int64_t acc = int64_t(int32_t(c->macl)) + prod;
        if (acc > 0x7FFFFFFFll) { acc = 0x7FFFFFFFll; c->mach |= 1; }
        else if (acc < -0x80000000ll) { acc = -0x80000000ll; c->mach |= 1; }
        c->macl = uint32_t(acc);
    } else {
        const int64_t acc = int64_t((uint64_t(c->mach) << 32) | c->macl) + prod;
        c->mach = uint32_t(uint64_t(acc) >> 32);
        c->macl = uint32_t(acc);
    }
}
BS_OP(MULL) { c->macl = c->r[n] * c->r[m]; }
BS_OP(MULSW) { c->macl = uint32_t(int32_t(int16_t(c->r[n])) * int32_t(int16_t(c->r[m]))); }
BS_OP(MULUW) { c->macl = uint32_t(uint16_t(c->r[n])) * uint32_t(uint16_t(c->r[m])); }
BS_OP(NEG) { c->r[n] = 0u - c->r[m]; }
BS_OP(NEGC) {
    const uint32_t tmp = 0u - c->r[m], r = tmp - c->t;
    c->t = (0u < tmp) || (tmp < r);
    c->r[n] = r;
}
BS_OP(SUB) { c->r[n] -= c->r[m]; }
BS_OP(SUBC) {
    const uint32_t a = c->r[n], s = a - c->r[m], r = s - c->t;
    c->t = (a < s) || (s < r);
    c->r[n] = r;
}
BS_OP(SUBV) {
    const uint32_t a = c->r[n], b = c->r[m], r = a - b;
    c->t = ((a ^ b) & (a ^ r)) >> 31;
    c->r[n] = r;
}

// ---------- logica ----------
BS_OP(AND) { c->r[n] &= c->r[m]; }
BS_OP(AND_IMM) { c->r[0] &= i; }
BS_OP(ANDB) { const uint32_t a = c->gbr + c->r[0]; sh4_write8(c, a, sh4_read8(c, a) & i); }
BS_OP(NOT) { c->r[n] = ~c->r[m]; }
BS_OP(OR) { c->r[n] |= c->r[m]; }
BS_OP(OR_IMM) { c->r[0] |= i; }
BS_OP(ORB) { const uint32_t a = c->gbr + c->r[0]; sh4_write8(c, a, sh4_read8(c, a) | i); }
BS_OP(TASB) {
    const uint32_t a = c->r[n];
    const uint8_t  v = sh4_read8(c, a);
    c->t = v == 0;
    sh4_write8(c, a, v | 0x80);
}
BS_OP(TST) { c->t = (c->r[n] & c->r[m]) == 0; }
BS_OP(TST_IMM) { c->t = (c->r[0] & i) == 0; }
BS_OP(TSTB) { c->t = (sh4_read8(c, c->gbr + c->r[0]) & i) == 0; }
BS_OP(XOR) { c->r[n] ^= c->r[m]; }
BS_OP(XOR_IMM) { c->r[0] ^= i; }
BS_OP(XORB) { const uint32_t a = c->gbr + c->r[0]; sh4_write8(c, a, sh4_read8(c, a) ^ i); }

// ---------- deslocamento ----------
BS_OP(ROTL) { const uint32_t v = c->r[n]; c->t = v >> 31; c->r[n] = (v << 1) | c->t; }
BS_OP(ROTR) { const uint32_t v = c->r[n]; c->t = v & 1; c->r[n] = (v >> 1) | (c->t << 31); }
BS_OP(ROTCL) { const uint32_t v = c->r[n]; c->r[n] = (v << 1) | c->t; c->t = v >> 31; }
BS_OP(ROTCR) { const uint32_t v = c->r[n]; c->r[n] = (v >> 1) | (c->t << 31); c->t = v & 1; }
BS_OP(SHAD) {
    const int32_t s = int32_t(c->r[m]);
    const int32_t v = int32_t(c->r[n]);
    if (s >= 0) c->r[n] = uint32_t(v) << (s & 31);
    else if ((s & 31) == 0) c->r[n] = v < 0 ? 0xFFFFFFFFu : 0;
    else c->r[n] = uint32_t(v >> ((~s & 31) + 1));
}
BS_OP(SHAL) { c->t = c->r[n] >> 31; c->r[n] <<= 1; }
BS_OP(SHAR) { c->t = c->r[n] & 1; c->r[n] = uint32_t(int32_t(c->r[n]) >> 1); }
BS_OP(SHLD) {
    const int32_t s = int32_t(c->r[m]);
    if (s >= 0) c->r[n] <<= (s & 31);
    else if ((s & 31) == 0) c->r[n] = 0;
    else c->r[n] >>= ((~s & 31) + 1);
}
BS_OP(SHLL) { c->t = c->r[n] >> 31; c->r[n] <<= 1; }
BS_OP(SHLR) { c->t = c->r[n] & 1; c->r[n] >>= 1; }
BS_OP(SHLL2) { c->r[n] <<= 2; }
BS_OP(SHLR2) { c->r[n] >>= 2; }
BS_OP(SHLL8) { c->r[n] <<= 8; }
BS_OP(SHLR8) { c->r[n] >>= 8; }
BS_OP(SHLL16) { c->r[n] <<= 16; }
BS_OP(SHLR16) { c->r[n] >>= 16; }

// ---------- controle do sistema ----------
BS_OP(CLRMAC) { c->mach = c->macl = 0; }
BS_OP(CLRS) { setsrbit(c, kSrS, false); }
BS_OP(CLRT) { c->t = 0; }
BS_OP(LDC_SR) { sh4_set_sr(*c, c->r[m]); }
BS_OP(LDC_GBR) { c->gbr = c->r[m]; }
BS_OP(LDC_VBR) { c->vbr = c->r[m]; }
BS_OP(LDC_SSR) { c->ssr = c->r[m]; }
BS_OP(LDC_SPC) { c->spc = c->r[m]; }
BS_OP(LDC_DBR) { c->dbr = c->r[m]; }
BS_OP(LDC_BANK) { c->rbank[n] = c->r[m]; }
// ldc.l @Rm+: incrementa antes de aplicar SR (uma troca de banco nao afeta o Rm usado).
#define BS_POP(c, m) ([&] { const uint32_t v_ = sh4_read32(c, c->r[m]); c->r[m] += 4; return v_; }())
BS_OP(LDCL_SR) { const uint32_t v = BS_POP(c, m); sh4_set_sr(*c, v); }
BS_OP(LDCL_GBR) { c->gbr = BS_POP(c, m); }
BS_OP(LDCL_VBR) { c->vbr = BS_POP(c, m); }
BS_OP(LDCL_SSR) { c->ssr = BS_POP(c, m); }
BS_OP(LDCL_SPC) { c->spc = BS_POP(c, m); }
BS_OP(LDCL_DBR) { c->dbr = BS_POP(c, m); }
BS_OP(LDCL_BANK) { c->rbank[n] = BS_POP(c, m); }
BS_OP(LDS_MACH) { c->mach = c->r[m]; }
BS_OP(LDS_MACL) { c->macl = c->r[m]; }
BS_OP(LDS_PR) { c->pr = c->r[m]; }
BS_OP(LDSL_MACH) { c->mach = BS_POP(c, m); }
BS_OP(LDSL_MACL) { c->macl = BS_POP(c, m); }
BS_OP(LDSL_PR) { c->pr = BS_POP(c, m); }
BS_OP(LDTLB) { sh4_ldtlb(c); }
BS_OP(NOP) {}
BS_OP(OCBI) {}
BS_OP(OCBP) {}
BS_OP(OCBWB) {}
BS_OP(PREF) { if ((c->r[n] & 0xFC000000u) == 0xE0000000u) sh4_sq_flush(c, c->r[n]); }
BS_OP(SETS) { setsrbit(c, kSrS, true); }
BS_OP(SETT) { c->t = 1; }
BS_OP(SLEEP) { sh4_sleep(c); }
BS_OP(STC_SR) { c->r[n] = sh4_get_sr(*c); }
BS_OP(STC_GBR) { c->r[n] = c->gbr; }
BS_OP(STC_VBR) { c->r[n] = c->vbr; }
BS_OP(STC_SSR) { c->r[n] = c->ssr; }
BS_OP(STC_SPC) { c->r[n] = c->spc; }
BS_OP(STC_SGR) { c->r[n] = c->sgr; }
BS_OP(STC_DBR) { c->r[n] = c->dbr; }
BS_OP(STC_BANK) { c->r[n] = c->rbank[m]; }
#define BS_PUSH(c, n, v) do { const uint32_t a_ = c->r[n] - 4; sh4_write32(c, a_, (v)); c->r[n] = a_; } while (0)
BS_OP(STCL_SR) { BS_PUSH(c, n, sh4_get_sr(*c)); }
BS_OP(STCL_GBR) { BS_PUSH(c, n, c->gbr); }
BS_OP(STCL_VBR) { BS_PUSH(c, n, c->vbr); }
BS_OP(STCL_SSR) { BS_PUSH(c, n, c->ssr); }
BS_OP(STCL_SPC) { BS_PUSH(c, n, c->spc); }
BS_OP(STCL_SGR) { BS_PUSH(c, n, c->sgr); }
BS_OP(STCL_DBR) { BS_PUSH(c, n, c->dbr); }
BS_OP(STCL_BANK) { BS_PUSH(c, n, c->rbank[m]); }
BS_OP(STS_MACH) { c->r[n] = c->mach; }
BS_OP(STS_MACL) { c->r[n] = c->macl; }
BS_OP(STS_PR) { c->r[n] = c->pr; }
BS_OP(STSL_MACH) { BS_PUSH(c, n, c->mach); }
BS_OP(STSL_MACL) { BS_PUSH(c, n, c->macl); }
BS_OP(STSL_PR) { BS_PUSH(c, n, c->pr); }
BS_OP(TRAPA) { sh4_trapa(c, i); }

// ---------- ponto flutuante ----------
// FMOV: com SZ=1 move 64 bits; registrador impar = XD (banco XF).
BS_OP(FMOV) {
    if (!sz(c)) { c->fr[n] = c->fr[m]; return; }
    const uint32_t* s = pair(c, m);
    uint32_t*       d = pair(c, n);
    const uint32_t  a = s[0], b = s[1];
    d[0] = a;
    d[1] = b;
}
inline void fload(C* c, uint32_t n, uint32_t addr) {
    if (!sz(c)) { c->fr[n] = sh4_read32(c, addr); return; }
    uint32_t* d = pair(c, n);
    d[0]        = sh4_read32(c, addr);
    d[1]        = sh4_read32(c, addr + 4);
}
inline void fstore(C* c, uint32_t m, uint32_t addr) {
    if (!sz(c)) { sh4_write32(c, addr, c->fr[m]); return; }
    const uint32_t* s = pair(c, m);
    sh4_write32(c, addr, s[0]);
    sh4_write32(c, addr + 4, s[1]);
}
BS_OP(FMOVS_LD) { fload(c, n, c->r[m]); }
BS_OP(FMOVS_ST) { fstore(c, m, c->r[n]); }
BS_OP(FMOVS_POSTINC) { const uint32_t a = c->r[m]; c->r[m] += sz(c) ? 8 : 4; fload(c, n, a); }
BS_OP(FMOVS_PREDEC) { const uint32_t a = c->r[n] - (sz(c) ? 8 : 4); fstore(c, m, a); c->r[n] = a; }
BS_OP(FMOVS_LD_R0) { fload(c, n, c->r[0] + c->r[m]); }
BS_OP(FMOVS_ST_R0) { fstore(c, m, c->r[0] + c->r[n]); }
BS_OP(FLDI0) { c->fr[n] = 0x00000000u; }
BS_OP(FLDI1) { c->fr[n] = 0x3F800000u; }
BS_OP(FLDS) { c->fpul = c->fr[m]; }
BS_OP(FSTS) { c->fr[n] = c->fpul; }
BS_OP(FABS) { c->fr[pr(c) ? (n & 14) : n] &= 0x7FFFFFFFu; }
BS_OP(FNEG) { c->fr[pr(c) ? (n & 14) : n] ^= 0x80000000u; }
#define BS_FBIN(name, expr)                                                  \
    BS_OP(name) {                                                            \
        if (pr(c)) { const double a = getDR(c, n), b = getDR(c, m); setDR(c, n, (expr)); } \
        else { const float a = f32(c->fr[n]), b = f32(c->fr[m]); c->fr[n] = u32(expr); } \
    }
BS_FBIN(FADD, a + b)
BS_FBIN(FSUB, a - b)
BS_FBIN(FMUL, a * b)
BS_FBIN(FDIV, a / b)
#undef BS_FBIN
BS_OP(FMAC) {
    const float p = f32(c->fr[0]) * f32(c->fr[m]);
    c->fr[n]      = u32(p + f32(c->fr[n]));
}
BS_OP(FCMPEQ) { c->t = pr(c) ? getDR(c, n) == getDR(c, m) : f32(c->fr[n]) == f32(c->fr[m]); }
BS_OP(FCMPGT) { c->t = pr(c) ? getDR(c, n) > getDR(c, m) : f32(c->fr[n]) > f32(c->fr[m]); }
BS_OP(FLOAT) {
    if (pr(c)) setDR(c, n, double(int32_t(c->fpul)));
    else c->fr[n] = u32(float(int32_t(c->fpul)));
}
BS_OP(FTRC) { c->fpul = pr(c) ? ftrc_sat(getDR(c, m)) : ftrc_sat(double(f32(c->fr[m]))); }
BS_OP(FSQRT) {
    if (pr(c)) setDR(c, n, std::sqrt(getDR(c, n)));
    else c->fr[n] = u32(std::sqrt(f32(c->fr[n])));
}
BS_OP(FSRRA) { c->fr[n] = u32(1.0f / std::sqrt(f32(c->fr[n]))); }
BS_OP(FCNVSD) { setDR(c, n * 2, double(f32(c->fpul))); }
BS_OP(FCNVDS) { c->fpul = u32(float(getDR(c, m * 2))); }
BS_OP(FIPR) {
    const uint32_t a = n * 4, b = m * 4;
    float          s = 0;
    for (uint32_t k = 0; k < 4; ++k) s += f32(c->fr[b + k]) * f32(c->fr[a + k]);
    c->fr[a + 3] = u32(s);
}
// FTRV: FVn = XMTRX * FVn, com XMTRX em XF0..XF15 por colunas.
BS_OP(FTRV) {
    const uint32_t a = n * 4;
    float          v[4], r[4];
    for (uint32_t k = 0; k < 4; ++k) v[k] = f32(c->fr[a + k]);
    for (uint32_t row = 0; row < 4; ++row) {
        float s = 0;
        for (uint32_t k = 0; k < 4; ++k) s += f32(c->xf[row + 4 * k]) * v[k];
        r[row] = s;
    }
    for (uint32_t k = 0; k < 4; ++k) c->fr[a + k] = u32(r[k]);
}
// FSCA: angulo de 16 bits em FPUL (0x10000 = 2*pi); seno em FR[2n], cosseno em FR[2n+1].
// Tabela (como no hardware), independente do modo de arredondamento do host.
BS_OP(FSCA) {
    const float* t   = sh4_fsca_table() + 2 * (c->fpul & 0xFFFF);
    c->fr[n * 2]     = u32(t[0]);
    c->fr[n * 2 + 1] = u32(t[1]);
}
BS_OP(FRCHG) { sh4_set_fpscr(*c, c->fpscr ^ kFpscrFR); }
BS_OP(FSCHG) { c->fpscr ^= kFpscrSZ; }
BS_OP(LDS_FPSCR) { sh4_set_fpscr(*c, c->r[m]); }
BS_OP(LDS_FPUL) { c->fpul = c->r[m]; }
BS_OP(LDSL_FPSCR) { const uint32_t v = BS_POP(c, m); sh4_set_fpscr(*c, v); }
BS_OP(LDSL_FPUL) { c->fpul = BS_POP(c, m); }
BS_OP(STS_FPSCR) { c->r[n] = c->fpscr; }
BS_OP(STS_FPUL) { c->r[n] = c->fpul; }
BS_OP(STSL_FPSCR) { BS_PUSH(c, n, c->fpscr); }
BS_OP(STSL_FPUL) { BS_PUSH(c, n, c->fpul); }

#undef BS_POP
#undef BS_PUSH

// Desvios: tratados pelo gerador.
#define BS_CONTROL(name) BS_OP(name) { control_flow(c, n, m, i); }
BS_CONTROL(BF) BS_CONTROL(BFS) BS_CONTROL(BT) BS_CONTROL(BTS) BS_CONTROL(BRA) BS_CONTROL(BRAF)
BS_CONTROL(BSR) BS_CONTROL(BSRF) BS_CONTROL(JMP) BS_CONTROL(JSR) BS_CONTROL(RTS) BS_CONTROL(RTE)
#undef BS_CONTROL
#undef BS_OP

} // namespace bs::op
