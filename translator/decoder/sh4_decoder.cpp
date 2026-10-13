#include "decoder/sh4_decoder.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace bs::sh4 {
namespace {

constexpr OpInfo kInvalidInfo = {"INVALID", "", ".word", "", ImmKind::NONE, 0};

constexpr OpInfo kInfos[] = {
    kInvalidInfo,
#define X(name, pat, mnem, ops, imm, flags) {#name, pat, mnem, ops, ImmKind::imm, static_cast<uint16_t>(flags)},
#include "decoder/sh4_isa.inc"
#undef X
};
static_assert(std::size(kInfos) == size_t(Op::Count));

struct Pattern {
    uint16_t mask = 0, value = 0;
};

Pattern compile(const char* pat) {
    if (std::strlen(pat) != 16) throw std::logic_error(std::string("padrao SH-4 sem 16 bits: ") + pat);
    Pattern p;
    for (int i = 0; i < 16; ++i) {
        const uint16_t bit = uint16_t(1u << (15 - i));
        if (pat[i] == '0' || pat[i] == '1') {
            p.mask |= bit;
            if (pat[i] == '1') p.value |= bit;
        }
    }
    return p;
}

// Junta os bits marcados com `c`, do mais significativo ao menos.
uint32_t field(const char* pat, uint16_t op, char c, int* width) {
    uint32_t v = 0;
    int      w = 0;
    for (int i = 0; i < 16; ++i)
        if (pat[i] == c) {
            v = (v << 1) | ((op >> (15 - i)) & 1);
            ++w;
        }
    if (width) *width = w;
    return v;
}

int32_t normalize(ImmKind k, uint32_t raw) {
    switch (k) {
    case ImmKind::NONE: return 0;
    case ImmKind::S8: return int8_t(raw);
    case ImmKind::U8: return int32_t(raw);
    case ImmKind::D4x1: case ImmKind::D8x1: return int32_t(raw);
    case ImmKind::D4x2: case ImmKind::D8x2: case ImmKind::PC8x2: return int32_t(raw * 2);
    case ImmKind::D4x4: case ImmKind::D8x4: case ImmKind::PC8x4: return int32_t(raw * 4);
    case ImmKind::BR8: return int32_t(int8_t(raw)) * 2;
    case ImmKind::BR12: return (int32_t(raw << 20) >> 20) * 2;
    }
    return 0;
}

struct Table {
    std::array<Op, 65536>    ops{};
    std::vector<std::string> conflicts;

    Table() {
        ops.fill(Op::Invalid);
        for (size_t i = 1; i < std::size(kInfos); ++i) {
            const Pattern p = compile(kInfos[i].pattern);
            for (uint32_t op = 0; op < 65536; ++op) {
                if ((op & p.mask) != p.value) continue;
                if (ops[op] != Op::Invalid)
                    conflicts.push_back(std::string(kInfos[size_t(ops[op])].name) + " x " + kInfos[i].name);
                else
                    ops[op] = Op(i);
            }
        }
    }
};

const Table& table() {
    static const Table t;
    return t;
}

} // namespace

const OpInfo& opInfo(Op op) { return kInfos[size_t(op) < std::size(kInfos) ? size_t(op) : 0]; }
const OpInfo& Insn::info() const { return opInfo(op); }

Insn decode(uint16_t opcode) {
    Insn i;
    i.raw = opcode;
    i.op  = table().ops[opcode];
    if (i.op == Op::Invalid) return i;
    const OpInfo& inf = i.info();
    i.rn              = uint8_t(field(inf.pattern, opcode, 'n', nullptr));
    i.rm              = uint8_t(field(inf.pattern, opcode, 'm', nullptr));
    int      w        = 0;
    uint32_t raw      = field(inf.pattern, opcode, 'i', &w);
    if (w == 0) raw = field(inf.pattern, opcode, 'd', &w);
    i.imm = normalize(inf.imm, raw);
    return i;
}

uint32_t target(const Insn& insn, uint32_t pc) {
    switch (insn.info().imm) {
    case ImmKind::PC8x4: return (pc & ~3u) + 4 + uint32_t(insn.imm);
    case ImmKind::PC8x2:
    case ImmKind::BR8:
    case ImmKind::BR12: return pc + 4 + uint32_t(insn.imm);
    default: throw std::logic_error("target() em instrucao sem alvo relativo ao PC");
    }
}

std::string disasm(const Insn& insn, uint32_t pc) {
    char buf[32];
    if (!insn.valid()) {
        std::snprintf(buf, sizeof(buf), ".word 0x%04x", insn.raw);
        return buf;
    }
    const OpInfo& inf = insn.info();
    std::string   s   = inf.mnemonic;
    if (*inf.operands) s += ' ';
    for (const char* p = inf.operands; *p; ++p) {
        if (*p != '%') {
            s += *p;
            continue;
        }
        const char c = *++p;
        auto reg = [&](char kind, uint8_t r) {
            switch (kind) {
            case 'F': std::snprintf(buf, sizeof(buf), "fr%u", r); break;
            case 'D': std::snprintf(buf, sizeof(buf), "dr%u", r * 2); break;
            case 'V': std::snprintf(buf, sizeof(buf), "fv%u", r * 4); break;
            case 'B': std::snprintf(buf, sizeof(buf), "r%u_bank", r); break;
            }
            s += buf;
        };
        switch (c) {
        case 'n': std::snprintf(buf, sizeof(buf), "r%u", insn.rn); s += buf; break;
        case 'm': std::snprintf(buf, sizeof(buf), "r%u", insn.rm); s += buf; break;
        case 'F': case 'D': case 'V': case 'B': {
            const char which = *++p;
            reg(c, which == 'n' ? insn.rn : insn.rm);
            break;
        }
        case 'i':
        case 'd': std::snprintf(buf, sizeof(buf), "%d", insn.imm); s += buf; break;
        case 't': std::snprintf(buf, sizeof(buf), "0x%08x", target(insn, pc)); s += buf; break;
        default: throw std::logic_error(std::string("token de operando desconhecido em ") + inf.name);
        }
    }
    return s;
}

std::vector<std::string> tableConflicts() { return table().conflicts; }

} // namespace bs::sh4
