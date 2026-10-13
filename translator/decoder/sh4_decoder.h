#pragma once
// Decodificador SH-4: opcode de 16 bits -> Insn. Tabela completa de 65 536
// entradas gerada de sh4_isa.inc na primeira chamada.
#include <cstdint>
#include <string>
#include <vector>

namespace bs::sh4 {

enum class Op : uint16_t {
    Invalid = 0,
#define X(name, pat, mnem, ops, imm, flags) name,
#include "decoder/sh4_isa.inc"
#undef X
    Count
};

// Como o campo i/d vira Insn::imm.
enum class ImmKind : uint8_t {
    NONE,
    S8,     // imediato de 8 bits com sinal
    U8,     // imediato de 8 bits sem sinal
    D4x1, D4x2, D4x4,  // deslocamento de 4 bits sem sinal, em bytes
    D8x1, D8x2, D8x4,  // deslocamento de 8 bits sem sinal, em bytes (GBR)
    PC8x2,  // @(disp,PC) word:  alvo = PC + 4 + imm
    PC8x4,  // @(disp,PC) long:  alvo = (PC & ~3) + 4 + imm
    BR8,    // desvio de 8 bits com sinal:  alvo = PC + 4 + imm
    BR12,   // desvio de 12 bits com sinal: alvo = PC + 4 + imm
};

enum Flags : uint16_t {
    F_BRANCH       = 1 << 0,  // altera o fluxo
    F_COND         = 1 << 1,  // depende de T
    F_DELAYED      = 1 << 2,  // tem delay slot
    F_CALL         = 1 << 3,  // grava PR
    F_RETURN       = 1 << 4,  // rts/rte
    F_INDIRECT     = 1 << 5,  // alvo vem de registrador
    F_SLOT_ILLEGAL = 1 << 6,  // ilegal dentro de delay slot
    F_PRIV         = 1 << 7,  // exige modo privilegiado
    F_FPU          = 1 << 8,
    F_PCREL        = 1 << 9,  // le/calcula endereco relativo ao PC
};

struct OpInfo {
    const char* name;      // identificador (p.ex. "MOVL_PC")
    const char* pattern;   // padrao de 16 caracteres
    const char* mnemonic;  // p.ex. "mov.l"
    const char* operands;  // modelo de disassembly
    ImmKind     imm;
    uint16_t    flags;
};

struct Insn {
    Op       op  = Op::Invalid;
    uint8_t  rn  = 0;   // campo 'n' (4, 3 ou 2 bits conforme a instrucao)
    uint8_t  rm  = 0;   // campo 'm'
    int32_t  imm = 0;   // imediato/deslocamento normalizado (ver ImmKind)
    uint16_t raw = 0;

    const OpInfo& info() const;
    bool          valid() const { return op != Op::Invalid; }
    bool          has(uint16_t f) const { return (info().flags & f) != 0; }
};

const OpInfo& opInfo(Op op);
Insn          decode(uint16_t opcode);

// Alvo de desvio direto ou endereco PC-relativo (so para ImmKind PC8x2/PC8x4/BR8/BR12).
uint32_t target(const Insn& insn, uint32_t pc);

// Disassembly em minusculas, estilo GNU: "mov.l @(8,r4),r5", "mov.l 0x8c010018,r1", "bra 0x8c010020".
std::string disasm(const Insn& insn, uint32_t pc);

// Para testes: pares de padroes que casam com o mesmo opcode (deve ser vazio).
std::vector<std::string> tableConflicts();

} // namespace bs::sh4
