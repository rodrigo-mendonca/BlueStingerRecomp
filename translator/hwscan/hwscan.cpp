#include "hwscan/hwscan.h"

#include <algorithm>
#include <array>
#include <optional>

namespace bs::hwscan {

using analysis::Analysis;
using analysis::Function;
using analysis::Image;
using sh4::Insn;
using sh4::Op;

const char* kind_name(Kind k) {
    switch (k) {
    case Kind::Read: return "leitura";
    case Kind::Write: return "escrita";
    case Kind::Pref: return "pref";
    case Kind::Cache: return "cache";
    case Kind::Constant: return "constante";
    }
    return "?";
}

std::string string_at(const Image& img, uint32_t addr) {
    const uint32_t a = img.normalize(addr);
    if (!a) return {};
    std::string s;
    for (uint32_t p = a; img.contains(p) && s.size() < 200; ++p) {
        const uint8_t ch = img.byte(p);
        if (ch == 0) break;
        if (ch < 0x20 || ch > 0x7E) {
            if (ch == '\n' || ch == '\r' || ch == '\t') {
                s += ' ';
                continue;
            }
            return {};
        }
        s += char(ch);
    }
    return s.size() >= 4 ? s : std::string();
}

namespace {

struct Mem {
    enum Mode { None, Base, BaseDisp, R0Base, PostInc, PreDec } mode = None;
    int     base   = -1;
    uint8_t size   = 0;
    Kind    kind   = Kind::Read;
};

// Forma de enderecamento das instrucoes que tocam memoria (sem GBR: GBR desconhecido aqui).
Mem memOf(const Insn& in) {
    Mem m;
    auto set = [&](Mem::Mode mode, int base, uint8_t size, Kind kind) {
        m.mode = mode, m.base = base, m.size = size, m.kind = kind;
    };
    switch (in.op) {
    case Op::MOVB_ST: set(Mem::Base, in.rn, 1, Kind::Write); break;
    case Op::MOVW_ST: set(Mem::Base, in.rn, 2, Kind::Write); break;
    case Op::MOVL_ST: set(Mem::Base, in.rn, 4, Kind::Write); break;
    case Op::MOVB_LD: set(Mem::Base, in.rm, 1, Kind::Read); break;
    case Op::MOVW_LD: set(Mem::Base, in.rm, 2, Kind::Read); break;
    case Op::MOVL_LD: set(Mem::Base, in.rm, 4, Kind::Read); break;
    case Op::MOVB_PREDEC: set(Mem::PreDec, in.rn, 1, Kind::Write); break;
    case Op::MOVW_PREDEC: set(Mem::PreDec, in.rn, 2, Kind::Write); break;
    case Op::MOVL_PREDEC: set(Mem::PreDec, in.rn, 4, Kind::Write); break;
    case Op::MOVB_POSTINC: set(Mem::PostInc, in.rm, 1, Kind::Read); break;
    case Op::MOVW_POSTINC: set(Mem::PostInc, in.rm, 2, Kind::Read); break;
    case Op::MOVL_POSTINC: set(Mem::PostInc, in.rm, 4, Kind::Read); break;
    case Op::MOVB_ST_DISP: set(Mem::BaseDisp, in.rn, 1, Kind::Write); break;
    case Op::MOVW_ST_DISP: set(Mem::BaseDisp, in.rn, 2, Kind::Write); break;
    case Op::MOVL_ST_DISP: set(Mem::BaseDisp, in.rn, 4, Kind::Write); break;
    case Op::MOVB_LD_DISP: set(Mem::BaseDisp, in.rm, 1, Kind::Read); break;
    case Op::MOVW_LD_DISP: set(Mem::BaseDisp, in.rm, 2, Kind::Read); break;
    case Op::MOVL_LD_DISP: set(Mem::BaseDisp, in.rm, 4, Kind::Read); break;
    case Op::MOVB_ST_R0: set(Mem::R0Base, in.rn, 1, Kind::Write); break;
    case Op::MOVW_ST_R0: set(Mem::R0Base, in.rn, 2, Kind::Write); break;
    case Op::MOVL_ST_R0: set(Mem::R0Base, in.rn, 4, Kind::Write); break;
    case Op::MOVB_LD_R0: set(Mem::R0Base, in.rm, 1, Kind::Read); break;
    case Op::MOVW_LD_R0: set(Mem::R0Base, in.rm, 2, Kind::Read); break;
    case Op::MOVL_LD_R0: set(Mem::R0Base, in.rm, 4, Kind::Read); break;
    case Op::MOVCA: set(Mem::Base, in.rn, 4, Kind::Write); break;
    case Op::TASB: set(Mem::Base, in.rn, 1, Kind::Write); break;
    case Op::OCBI:
    case Op::OCBP:
    case Op::OCBWB: set(Mem::Base, in.rn, 32, Kind::Cache); break;
    case Op::PREF: set(Mem::Base, in.rn, 32, Kind::Pref); break;
    // fmov: 4 ou 8 bytes conforme FPSCR.SZ, que nao e propagado aqui. O acesso e registrado com
    // 8 bytes (cobre os dois casos) e o pos/pre-incremento invalida a base (4 ou 8; revisao Codex)
    case Op::FMOVS_LD: set(Mem::Base, in.rm, 8, Kind::Read); break;
    case Op::FMOVS_ST: set(Mem::Base, in.rn, 8, Kind::Write); break;
    case Op::FMOVS_POSTINC: set(Mem::PostInc, in.rm, 8, Kind::Read); break;
    case Op::FMOVS_PREDEC: set(Mem::PreDec, in.rn, 8, Kind::Write); break;
    case Op::FMOVS_LD_R0: set(Mem::R0Base, in.rm, 8, Kind::Read); break;
    case Op::FMOVS_ST_R0: set(Mem::R0Base, in.rn, 8, Kind::Write); break;
    default: break;
    }
    return m;
}

} // namespace

// Literal do pool que tem forma de endereco de hardware. Sem isso, mascaras (0xE0000000,
// 0xF0000000, 0x80000000), floats (0x40000000 = 2.0f, 0x4F000000) e padroes de preenchimento
// (0x80808080) caem em regioes de hardware. Acessos com endereco resolvido nao passam por aqui.
bool plausible_constant(uint32_t a) {
    using dcmap::Region;
    const Region r = dcmap::classify(a);
    if (!dcmap::is_hardware(r)) return false;
    if ((a >> 24) == (a & 0xFF) && (a >> 16 & 0xFF) == (a & 0xFF) && (a >> 8 & 0xFF) == (a & 0xFF)) return false;
    // enderecos de registro e de bloco sao alinhados; cores ARGB (0x80704820, 0xF7FFFFFF) muitas vezes nao
    if (a & 3) return false;
    // 0xFF000000: preto opaco em ARGB / mascara de byte, muito mais comum no codigo que o PTEH do CCN
    if (a == 0xFF000000u) return false;
    switch (r) {
    case Region::StoreQueue: return a != 0xE0000000u && (a & 31) == 0;  // base das SQ = mascara P3/P4
    case Region::CacheArrays: return a != 0xF0000000u && (a & 31) == 0;
    case Region::P4Other: return false;
    case Region::Ccn: case Region::Ubc: case Region::Bsc: case Region::Dmac: case Region::Cpg:
    case Region::Rtc: case Region::Intc: case Region::Tmu: case Region::Sci: case Region::Scif:
        return (a & 0x0007FF00u) == 0;  // registros nos primeiros 0x100 bytes do bloco (ou do meio-bloco +0x80000)
    default: break;
    }
    // TA em P0 (como o QACR das store queues o usa): so as bases com mais de um bit (0x10800000,
    // 0x11000000); 0x10000000 e 0x04000000 sozinhos sao mascaras de bit comuns
    if ((a & 0xE0000000u) == 0 && (r == Region::TaYuv || r == Region::TaTexture)) return (a & (a - 1)) != 0;
    // BIOS e flash sao lidos pelo espelho sem cache (P2); 0x80000060 etc. sao mascaras
    if (r == Region::BiosRom || r == Region::Flash) return (a & 0xE0000000u) == 0xA0000000u;
    // Area 0, AICA, VRAM, TA, RAM do sistema: so pelos espelhos P1/P2 (0x8.../0xA...), que e
    // como o codigo enderecava o hardware; P0 cru (0x005F6800) colide com constantes comuns
    const uint32_t seg = a & 0xE0000000u;
    if (seg != 0x80000000u && seg != 0xA0000000u) return false;
    if ((a & 0x1FFFFFFFu) == 0) return false;
    if (r == Region::BiosVectors || r == Region::BiosGdVector || r == Region::BiosFlashVector || r == Region::SystemArea)
        return (a >> 24) == 0x8C || (a >> 24) == 0xAC;
    return true;
}

namespace {

bool is_register_region(dcmap::Region r) {
    using dcmap::Region;
    switch (r) {
    case Region::HollySb: case Region::HollyIntc: case Region::Maple: case Region::GdRom: case Region::G1:
    case Region::G2: case Region::PvrDma: case Region::Pvr: case Region::AicaRegs: case Region::AicaRtc:
    case Region::BiosVectors: case Region::BiosGdVector: case Region::BiosFlashVector: case Region::Ccn: case Region::Ubc: case Region::Bsc: case Region::Dmac:
    case Region::Cpg: case Region::Rtc: case Region::Intc: case Region::Tmu: case Region::Sci: case Region::Scif:
        return true;
    default: return false;
    }
}

class FunctionScan {
public:
    FunctionScan(const Image& img, const Analysis& a, const Function& f, Scan& out)
        : img_(img), a_(a), f_(f), out_(out) {}

    void run() {
        std::vector<uint32_t> pcs(f_.insns.begin(), f_.insns.end());
        pcs.insert(pcs.end(), f_.slots.begin(), f_.slots.end());
        std::sort(pcs.begin(), pcs.end());
        int pendingReset = 0;  // 1 = zera r0-r7 depois do slot (chamada), 2 = zera tudo (desvio)
        for (uint32_t pc : pcs) {
            const bool isSlot = f_.slots.count(pc) > 0;
            if (!isSlot && f_.labels.count(pc)) reset(0xFFFF);  // juncao: nada se sabe
            const Insn in = sh4::decode(img_.read16(pc));
            step(pc, in);
            if (isSlot || !in.has(sh4::F_DELAYED)) {
                if (pendingReset == 1) reset(0x00FF);
                if (pendingReset == 2) reset(0xFFFF);
                pendingReset = 0;
            }
            if (in.has(sh4::F_BRANCH)) {
                edges(pc, in);
                const int r = in.has(sh4::F_CALL) ? 1 : (in.has(sh4::F_COND) ? 0 : 2);
                if (in.has(sh4::F_DELAYED)) pendingReset = std::max(pendingReset, r);
                else if (r == 1) reset(0x00FF);
                else if (r == 2) reset(0xFFFF);
            }
        }
    }

private:
    void reset(uint32_t mask) {
        for (int r = 0; r < 16; ++r)
            if (mask & (1u << r)) k_[r].reset();
    }

    void ref(uint32_t pc, uint32_t addr, uint8_t size, Kind kind) {
        const dcmap::Region reg = dcmap::classify(addr);
        if (!dcmap::is_hardware(reg)) return;
        out_.refs.push_back({f_.entry, pc, addr, size, kind, reg});
    }

    void edges(uint32_t pc, const Insn& in) {
        uint32_t target = 0;
        if (in.op == Op::BSR || in.op == Op::BRA) target = sh4::target(in, pc);
        else if (auto it = f_.direct.find(pc); it != f_.direct.end()) target = it->second;
        const bool tail = f_.tail_calls.count(pc) > 0;
        if (in.has(sh4::F_CALL)) {
            if (target) out_.edges.push_back({f_.entry, img_.normalize(target), pc, EdgeKind::Call});
            else out_.edges.push_back({f_.entry, 0, pc, EdgeKind::Indirect});
        } else if (tail) {
            out_.edges.push_back({f_.entry, target ? img_.normalize(target) : 0, pc,
                                  target ? EdgeKind::Tail : EdgeKind::Indirect});
        } else if (f_.unresolved.count(pc)) {
            out_.edges.push_back({f_.entry, 0, pc, EdgeKind::Indirect});
        }
    }

    void step(uint32_t pc, const Insn& in) {
        // acesso a memoria com endereco conhecido (antes de atualizar os registradores)
        const Mem m = memOf(in);
        if (m.mode != Mem::None && k_[m.base]) {
            uint32_t addr = *k_[m.base];
            bool     ok   = true;
            if (m.mode == Mem::BaseDisp) addr += uint32_t(in.imm);
            if (m.mode == Mem::PreDec) addr -= m.size;  // fmov: base-8, cobre os acessos de 4 e de 8 bytes
            if (m.mode == Mem::R0Base) {
                if (k_[0]) addr += *k_[0];
                else ok = false;
            }
            if (ok) ref(pc, addr, m.size, m.kind);
        }
        if (in.op == Op::PREF && !k_[in.rn]) out_.prefUnresolved[f_.entry]++;
        // desvios nao mudam registradores aqui: o efeito da chamada (r0-r7) e aplicado por run()
        // depois do delay slot, que ainda ve os valores de antes (p.ex. `jsr @r1; mov r8,r4`)
        if (in.has(sh4::F_BRANCH)) return;
        // valores
        std::optional<uint32_t> v;
        bool                    handled = true;
        switch (in.op) {
        case Op::MOV_IMM: v = uint32_t(in.imm); break;
        case Op::MOVL_PC: {
            const uint32_t lit = sh4::target(in, pc);
            if (img_.contains(lit, 4)) {
                v = img_.read32(lit);
                out_.literals.insert(lit);
                if (*v == 0xE0000000u) out_.sqLiteral[f_.entry]++;
                if (plausible_constant(*v)) ref(pc, *v, 4, Kind::Constant);  // endereco passado adiante
                if (std::string s = string_at(img_, *v); !s.empty())
                    out_.strings[f_.entry].push_back({pc, img_.normalize(*v), std::move(s)});
                if (const uint32_t t = img_.normalize(*v); t && a_.functions.count(t)) {
                    out_.addressTaken.insert(t);
                    out_.takenBy[t].insert(f_.entry);
                }
                else if (t && !a_.code.count(t)) out_.dataRefs[f_.entry].insert(t);
            }
            break;
        }
        case Op::MOVW_PC: {
            const uint32_t lit = sh4::target(in, pc);
            if (img_.contains(lit, 2)) v = uint32_t(int32_t(int16_t(img_.read16(lit))));
            break;
        }
        case Op::MOVA: {
            const uint32_t t = sh4::target(in, pc);
            k_[0] = t;
            if (const uint32_t n = img_.normalize(t); n && !a_.code.count(n)) out_.dataRefs[f_.entry].insert(n);
            if (std::string s = string_at(img_, t); !s.empty()) out_.strings[f_.entry].push_back({pc, t, std::move(s)});
            return;
        }
        case Op::MOV: v = k_[in.rm]; break;
        case Op::ADD_IMM:
            if (k_[in.rn]) v = *k_[in.rn] + uint32_t(in.imm);
            break;
        case Op::ADD:
            if (k_[in.rn] && k_[in.rm]) v = *k_[in.rn] + *k_[in.rm];
            break;
        case Op::SHLL: if (k_[in.rn]) v = *k_[in.rn] << 1; break;
        case Op::SHLL2: if (k_[in.rn]) v = *k_[in.rn] << 2; break;
        case Op::SHLL8: if (k_[in.rn]) v = *k_[in.rn] << 8; break;
        case Op::SHLL16: if (k_[in.rn]) v = *k_[in.rn] << 16; break;
        case Op::SHLR: if (k_[in.rn]) v = *k_[in.rn] >> 1; break;
        case Op::SHLR2: if (k_[in.rn]) v = *k_[in.rn] >> 2; break;
        case Op::SHLR8: if (k_[in.rn]) v = *k_[in.rn] >> 8; break;
        case Op::SHLR16: if (k_[in.rn]) v = *k_[in.rn] >> 16; break;
        case Op::OR_IMM: if (k_[0]) { k_[0] = *k_[0] | uint32_t(in.imm); } return;
        case Op::AND_IMM: if (k_[0]) { k_[0] = *k_[0] & uint32_t(in.imm); } return;
        default: handled = false; break;
        }
        const uint32_t writes = analysis::gprWrites(in);
        if (handled && in.op != Op::OR_IMM && in.op != Op::AND_IMM) {
            k_[in.rn] = v;
            return;
        }
        // o que escreve registrador vira desconhecido; pos/pre-incremento atualizam a base conhecida
        // (exceto `mov.l @r1+,r1`, em que o valor lido sobrescreve a base)
        std::optional<uint32_t> base;
        if ((m.mode == Mem::PostInc || m.mode == Mem::PreDec) && k_[m.base] && !in.has(sh4::F_FPU))
            base = m.mode == Mem::PostInc ? *k_[m.base] + m.size : *k_[m.base] - m.size;
        for (int r = 0; r < 16; ++r)
            if (writes & (1u << r)) k_[r].reset();
        const bool loadsIntoBase = m.mode == Mem::PostInc && !in.has(sh4::F_FPU) && in.rn == m.base;
        if (base && !loadsIntoBase) k_[m.base] = base;
    }

    const Image&                            img_;
    const Analysis&                         a_;
    const Function&                         f_;
    Scan&                                   out_;
    std::array<std::optional<uint32_t>, 16> k_{};
};

} // namespace

Scan scan(const Image& img, const Analysis& a) {
    Scan s;
    for (const auto& [entry, f] : a.functions) FunctionScan(img, a, f, s).run();
    // enderecos de hardware em palavras de dado (tabelas, estruturas inicializadas) fora do
    // codigo e dos pools ja vistos: funcao 0, pc = endereco da palavra
    for (uint32_t p = img.base; p + 4 <= img.base + uint32_t(img.bytes.size()); p += 4) {
        if (a.code.count(p) || a.code.count(p + 2) || s.literals.count(p)) continue;
        // Codigo nao alcancado e tabelas de pares de 16 bits tambem parecem enderecos: so
        // registros (blocos pequenos, alinhados) contam; nao enderecos no meio de VRAM/TA/ROM.
        const uint32_t      v = img.read32(p);
        const dcmap::Region r = dcmap::classify(v);
        if ((v & 3) || !is_register_region(r)) continue;
        if (plausible_constant(v)) s.refs.push_back({0, p, v, 4, Kind::Constant, r});
    }
    // funcoes descobertas por ponteiro (dado, pool, tabelas) tambem tem endereco "tomado"
    for (const auto& [entry, f] : a.functions)
        if (f.origin == analysis::Origin::Pointer || f.origin == analysis::Origin::DataPointer ||
            f.origin == analysis::Origin::PointerTable) {
            s.addressTaken.insert(entry);
            if (f.origin != analysis::Origin::Pointer || !s.takenBy.count(entry)) s.takenBy[entry].insert(0);
        }
    return s;
}

} // namespace bs::hwscan
