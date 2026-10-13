#pragma once
// Decodificador SH-4: opcode de 16 bits -> descricao da instrucao.
// Implementacao completa: marco M2.
#include <cstdint>

namespace bs::sh4 {

enum class Op : uint16_t {
    Invalid = 0,
    // M2: lista completa gerada a partir de uma tabela declarativa de padroes
};

struct Insn {
    Op      op  = Op::Invalid;
    uint8_t rn  = 0;
    uint8_t rm  = 0;
    int32_t imm = 0;
};

Insn decode(uint16_t opcode);

} // namespace bs::sh4
