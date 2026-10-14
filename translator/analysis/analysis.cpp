#include "analysis/analysis.h"

#include <algorithm>
#include <optional>

namespace bs::analysis {
using sh4::Insn;
using sh4::Op;

uint32_t Image::normalize(uint32_t a) const {
    const uint32_t phys = a & 0x1FFFFFFF, pbase = base & 0x1FFFFFFF;
    if (a >= 0xE0000000) return 0;
    if (phys >= pbase && phys - pbase < bytes.size()) return base + (phys - pbase);
    for (const auto& r : relocated)
        if (phys >= (r.dest & 0x1FFFFFFF) && phys - (r.dest & 0x1FFFFFFF) < r.size)
            return r.dest + (phys - (r.dest & 0x1FFFFFFF));
    return 0;
}

uint32_t gprWrites(const Insn& in) {
    const uint32_t N = 1u << in.rn, M = 1u << in.rm, R0 = 1;
    switch (in.op) {
    case Op::MOV_IMM: case Op::MOVW_PC: case Op::MOVL_PC: case Op::MOV:
    case Op::MOVB_LD: case Op::MOVW_LD: case Op::MOVL_LD: case Op::MOVL_LD_DISP:
    case Op::MOVB_LD_R0: case Op::MOVW_LD_R0: case Op::MOVL_LD_R0:
    case Op::MOVT: case Op::SWAPB: case Op::SWAPW: case Op::XTRCT:
    case Op::ADD: case Op::ADD_IMM: case Op::ADDC: case Op::ADDV: case Op::DIV1: case Op::DT:
    case Op::EXTSB: case Op::EXTSW: case Op::EXTUB: case Op::EXTUW:
    case Op::NEG: case Op::NEGC: case Op::SUB: case Op::SUBC: case Op::SUBV:
    case Op::AND: case Op::NOT: case Op::OR: case Op::XOR:
    case Op::ROTL: case Op::ROTR: case Op::ROTCL: case Op::ROTCR: case Op::SHAD: case Op::SHAL: case Op::SHAR:
    case Op::SHLD: case Op::SHLL: case Op::SHLR: case Op::SHLL2: case Op::SHLR2: case Op::SHLL8: case Op::SHLR8:
    case Op::SHLL16: case Op::SHLR16:
    case Op::STC_SR: case Op::STC_GBR: case Op::STC_VBR: case Op::STC_SSR: case Op::STC_SPC: case Op::STC_SGR:
    case Op::STC_DBR: case Op::STC_BANK: case Op::STS_MACH: case Op::STS_MACL: case Op::STS_PR:
    case Op::STS_FPSCR: case Op::STS_FPUL:
    case Op::MOVB_PREDEC: case Op::MOVW_PREDEC: case Op::MOVL_PREDEC:
    case Op::STCL_SR: case Op::STCL_GBR: case Op::STCL_VBR: case Op::STCL_SSR: case Op::STCL_SPC: case Op::STCL_SGR:
    case Op::STCL_DBR: case Op::STCL_BANK: case Op::STSL_MACH: case Op::STSL_MACL: case Op::STSL_PR:
    case Op::STSL_FPSCR: case Op::STSL_FPUL: case Op::FMOVS_PREDEC:
        return N;
    case Op::MOVB_POSTINC: case Op::MOVW_POSTINC: case Op::MOVL_POSTINC: case Op::MACL: case Op::MACW:
        return N | M;
    case Op::LDCL_GBR: case Op::LDCL_VBR: case Op::LDCL_SSR: case Op::LDCL_SPC: case Op::LDCL_DBR: case Op::LDCL_BANK:
    case Op::LDSL_MACH: case Op::LDSL_MACL: case Op::LDSL_PR: case Op::LDSL_FPSCR: case Op::LDSL_FPUL:
    case Op::FMOVS_POSTINC:
        return M;
    case Op::MOVB_LD_DISP: case Op::MOVW_LD_DISP: case Op::MOVB_LD_GBR: case Op::MOVW_LD_GBR: case Op::MOVL_LD_GBR:
    case Op::MOVA: case Op::AND_IMM: case Op::OR_IMM: case Op::XOR_IMM:
        return R0;
    case Op::LDC_SR: return 0xFF;       // troca de banco muda R0-R7
    case Op::LDCL_SR: return 0xFF | M;
    case Op::BSR: case Op::BSRF: case Op::JSR: case Op::TRAPA: return 0xFFFF;
    default: return 0;
    }
}

namespace {

constexpr int kBacktrack = 24;

class Analyzer {
public:
    Analyzer(const Image& img, const Config& cfg) : img_(img), cfg_(cfg) {}

    Analysis run() {
        for (auto [a, b] : cfg_.data_ranges)
            for (uint32_t x = a & ~1u; x < b; x += 2) out_.data.insert(x);
        addEntry(cfg_.entry, Origin::Entry, 0);
        for (uint32_t f : cfg_.extra_functions) addEntry(f, Origin::Config, 0);
        discover();
        for (;;) {  // ponto fixo: novas funcoes podem materializar novos ponteiros
            size_t added = 0;
            if (cfg_.pointer_candidates)
                for (uint32_t v : std::set<uint32_t>(poolValues_)) {
                    const uint32_t t = img_.normalize(v);
                    if (!t || entries_.count(t) || !looksLikeFunction(t) || !bodyIsCode(t)) continue;
                    addEntry(t, Origin::Pointer, 0);
                    ++added;
                }
            if (cfg_.data_pointers)
                for (uint32_t a = img_.base; img_.contains(a, 4); a += 4) {
                    if (visited_.count(a) && !out_.data.count(a)) continue;  // dentro de codigo
                    const uint32_t t = img_.normalize(img_.read32(a));
                    // ponteiro em dado + fronteira de funcao: basta codigo linear plausivel
                    // (callbacks sem prologo, p.ex. 8C15AFC0 = "mov.w; stc sr; ...")
                    if (!t || entries_.count(t) || !atBoundary(t)) continue;
                    if (!looksLikeFunction(t) && !plausibleCode(t, 2, true)) continue;
                    if (!bodyIsCode(t)) continue;
                    addEntry(t, Origin::DataPointer, 0);
                    ++added;
                }
            if (!added) break;
            discover();
        }
        for (auto& [addr, origin] : entries_) buildFunction(addr, origin);
        out_.code = visited_;
        checkOrphans();
        computeStats();
        return std::move(out_);
    }

private:
    // ---------- utilitarios ----------
    std::optional<Insn> at(uint32_t pc) const {
        if (!img_.contains(pc, 2) || (pc & 1)) return std::nullopt;
        Insn in = sh4::decode(img_.read16(pc));
        if (!in.valid()) return std::nullopt;
        return in;
    }

    void warn(std::string s) {
        if (out_.warnings.size() < 2000) out_.warnings.push_back(std::move(s));
    }

    static std::string hex(uint32_t v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%08X", v);
        return b;
    }

    void markPool(const Insn& in, uint32_t pc) {
        if (in.op == Op::MOVW_PC) {
            const uint32_t a = sh4::target(in, pc);
            if (img_.contains(a, 2)) out_.data.insert(a);
        } else if (in.op == Op::MOVL_PC) {
            const uint32_t a = sh4::target(in, pc);
            if (img_.contains(a, 4)) {
                out_.data.insert(a);
                out_.data.insert(a + 2);
                if (img_.inMain(a, 4)) poolValues_.insert(img_.read32(a));
            }
        }
    }

    // Valor constante de `reg` imediatamente antes de `pc`, andando para tras em
    // linha reta. Com `labels`, falha se cruzar um ponto de entrada de fluxo.
    std::optional<uint32_t> constBefore(uint32_t pc, uint32_t reg, const std::set<uint32_t>* labels) const {
        uint32_t a = pc;
        for (int k = 0; k < kBacktrack; ++k) {
            if (labels && a != pc && labels->count(a)) return std::nullopt;
            if (labels && a == pc && labels->count(a)) return std::nullopt;
            a -= 2;
            auto in = at(a);
            if (!in || in->has(sh4::F_BRANCH)) return std::nullopt;
            if (auto prev = at(a - 2); prev && prev->has(sh4::F_DELAYED)) return std::nullopt;  // `a` e delay slot
            if (!(gprWrites(*in) & (1u << reg))) continue;
            switch (in->op) {
            case Op::MOVL_PC: {
                if (in->rn != reg) return std::nullopt;
                const uint32_t p = sh4::target(*in, a);
                if (!img_.inMain(p, 4)) return std::nullopt;  // pool realocado e mutavel (corrigido na copia)
                return img_.read32(p);
            }
            case Op::MOV:
                if (in->rn != reg) return std::nullopt;
                reg = in->rm;
                continue;
            case Op::MOVA:
                if (reg != 0) return std::nullopt;
                return sh4::target(*in, a);
            case Op::OR: {
                // "mova L,r0; mov.l #0xA0000000,r1; or r0,r1; jmp @r1": salto para o
                // espelho P2 (sem cache) do proprio codigo, antes de mexer no CCR
                if (in->rn != reg) return std::nullopt;
                auto x = constBefore(a, in->rn, labels);
                auto y = x ? constBefore(a, in->rm, labels) : std::nullopt;
                if (!y) return std::nullopt;
                return *x | *y;
            }
            default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    void addEntry(uint32_t addr, Origin origin, uint32_t from) {
        const uint32_t t = img_.normalize(addr);
        if (!t) {
            if (external_.insert(addr).second && origin != Origin::Pointer)
                warn("chamada para fora da imagem: " + hex(addr) + (from ? " a partir de " + hex(from) : ""));
            return;
        }
        if (t & 1) {
            warn("alvo de chamada impar ignorado: " + hex(t));
            return;
        }
        if (entries_.emplace(t, origin).second) pending_.push_back(t);
    }

    // ---------- tabelas de salto ----------
    std::optional<JumpTable> matchJumpTable(uint32_t pc, const Insn& br) {
        for (const auto& o : cfg_.jump_tables)
            if (o.insn == pc) return readTable(pc, o.table, br.op == Op::BRAF ? 2 : 4, o.count, true);
        // braf rk  <- mov.w @(r0,ri),rk  <- mova T,r0   (relativo a braf+4)
        // jmp @rk  <- mov.l @(r0,ri),rk  <- mova T,r0   (absoluto)
        const Op load = br.op == Op::BRAF ? Op::MOVW_LD_R0 : Op::MOVL_LD_R0;
        auto     l    = at(pc - 2);
        auto     m    = at(pc - 4);
        if (l && m && l->op == load && l->rn == br.rn && m->op == Op::MOVA) {
            const uint32_t table = sh4::target(*m, pc - 4);
            return readTable(pc, table, br.op == Op::BRAF ? 2 : 4, boundBefore(pc - 4), false);
        }
        if (br.op == Op::JMP) return matchPoolJumpTable(pc, br);
        return std::nullopt;
    }

    // jmp @rk  <- mov.l @(r0,rb),rk  [+ ate 3 instrucoes que nao tocam rk]  com rb do pool
    // (copia desenrolada estilo Duff, 8C08D1D0). So destinos locais (ate 4 KB do jmp):
    // uma tabela de ponteiros para outras funcoes nao vira label desta.
    std::optional<JumpTable> matchPoolJumpTable(uint32_t pc, const Insn& br) {
        for (uint32_t a = pc - 2, k = 0; k < 4; ++k, a -= 2) {
            auto in = at(a);
            if (!in || in->has(sh4::F_BRANCH)) return std::nullopt;
            if (auto prev = at(a - 2); prev && prev->has(sh4::F_DELAYED)) return std::nullopt;
            if (!(gprWrites(*in) & (1u << br.rn))) continue;
            if (in->op != Op::MOVL_LD_R0 || in->rn != br.rn || in->rm == 0) return std::nullopt;
            auto base = constBefore(a, in->rm, nullptr);
            if (!base) return std::nullopt;
            const uint32_t table = img_.normalize(*base);
            if (!table) return std::nullopt;
            return readTable(pc, table, 4, boundBefore(a), false, true);
        }
        return std::nullopt;
    }

    // Procura `mov #N,rk` + `cmp/hs rk,ri` (N casos) ou `cmp/hi` (N+1) antes do indice.
    uint32_t boundBefore(uint32_t pc) const {
        uint32_t a = pc;
        for (int k = 0; k < 16; ++k) {
            a -= 2;
            auto in = at(a);
            if (!in) return 0;
            if (in->op == Op::CMPHS || in->op == Op::CMPHI) {
                const uint32_t rk = in->rm;
                uint32_t       b  = a;
                for (int j = 0; j < 8; ++j) {
                    b -= 2;
                    auto d = at(b);
                    if (!d) return 0;
                    if (gprWrites(*d) & (1u << rk)) {
                        if (d->rn != rk) return 0;
                        int64_t n = 0;
                        if (d->op == Op::MOV_IMM) n = d->imm;
                        else if (d->op == Op::MOVW_PC && img_.contains(sh4::target(*d, b), 2))
                            n = int16_t(img_.read16(sh4::target(*d, b)));
                        else if (d->op == Op::MOVL_PC && img_.contains(sh4::target(*d, b), 4))
                            n = int32_t(img_.read32(sh4::target(*d, b)));
                        if (n <= 0 || n > 4096) return 0;
                        return uint32_t(n) + (in->op == Op::CMPHI ? 1 : 0);
                    }
                }
                return 0;
            }
        }
        return 0;
    }

    // `localOnly`: destino a mais de 4 KB do desvio conta como entrada invalida.
    std::optional<JumpTable> readTable(uint32_t pc, uint32_t table, uint32_t esize, uint32_t count, bool forced,
                                       bool localOnly = false) {
        JumpTable jt;
        jt.insn       = pc;
        jt.table      = table;
        jt.entry_size = esize;
        jt.bounded    = count > 0;
        const uint32_t limit = count ? count : 256;
        for (uint32_t k = 0; k < limit; ++k) {
            const uint32_t ea = table + k * esize;
            if (!img_.contains(ea, esize)) break;
            uint32_t t = esize == 2 ? pc + 4 + uint32_t(int32_t(int16_t(img_.read16(ea)))) : img_.normalize(img_.read32(ea));
            const bool ok = t && !(t & 1) && img_.contains(t, 2) && at(t).has_value() &&
                            (!localOnly || (t + 0x1000 >= pc && t <= pc + 0x1000));
            if (!ok) {
                if (jt.bounded) {
                    warn("tabela de salto em " + hex(pc) + ": entrada " + std::to_string(k) + " invalida");
                    return std::nullopt;
                }
                break;
            }
            // Heuristica (sem limite): para quando a tabela encosta em codigo conhecido.
            if (!jt.bounded && (visited_.count(ea) || std::find(jt.targets.begin(), jt.targets.end(), ea) != jt.targets.end()))
                break;
            jt.targets.push_back(t);
        }
        if (jt.targets.empty()) return std::nullopt;
        (void)forced;
        for (uint32_t k = 0; k < jt.targets.size() * esize; k += 2) out_.data.insert(table + k);
        return jt;
    }

    // ---------- fase 1: descoberta global ----------
    void push(std::vector<uint32_t>& stack, uint32_t t, uint32_t from) {
        if (!reachedFrom_.count(t)) reachedFrom_[t] = from;
        stack.push_back(t);
    }

    void discover() {
        while (!pending_.empty()) {
            std::vector<uint32_t> stack{pending_.back()};
            pending_.pop_back();
            while (!stack.empty()) {
                uint32_t pc = stack.back();
                stack.pop_back();
                for (;;) {
                    if (visited_.count(pc)) break;
                    auto in = at(pc);
                    if (!in || out_.data.count(pc)) break;
                    visited_.insert(pc);
                    markPool(*in, pc);
                    if (!in->has(sh4::F_BRANCH)) {
                        pc += 2;
                        continue;
                    }
                    if (in->has(sh4::F_DELAYED)) {
                        auto slot = at(pc + 2);
                        if (!slot || slot->has(sh4::F_SLOT_ILLEGAL)) break;
                        // PC-relativa em delay slot: semantica especial nao suportada (0 casos no jogo).
                        if (slot->has(sh4::F_PCREL)) {
                            warn("instrucao PC-relativa em delay slot em " + hex(pc + 2) + " (nao suportado)");
                            break;
                        }
                        visited_.insert(pc + 2);
                        markPool(*slot, pc + 2);
                    }
                    const uint32_t next = pc + (in->has(sh4::F_DELAYED) ? 4 : 2);
                    bool           cont = false;
                    switch (in->op) {
                    case Op::BT: case Op::BF: case Op::BTS: case Op::BFS:
                        push(stack, sh4::target(*in, pc), pc);
                        cont = true;
                        break;
                    case Op::BRA: push(stack, sh4::target(*in, pc), pc); break;
                    case Op::BSR:
                        addEntry(sh4::target(*in, pc), Origin::Call, pc);
                        cont = true;
                        break;
                    case Op::JSR: case Op::BSRF:
                        if (auto v = constBefore(pc, in->rn, nullptr))
                            addEntry(in->op == Op::BSRF ? pc + 4 + *v : *v, Origin::Call, pc);
                        cont = true;
                        break;
                    case Op::JMP:
                        if (auto jt = matchJumpTable(pc, *in)) {
                            for (uint32_t t : jt->targets) push(stack, t, pc);
                            out_.jump_tables[pc] = std::move(*jt);
                        } else if (auto v = constBefore(pc, in->rn, nullptr)) {
                            addEntry(*v, Origin::Call, pc);
                        }
                        break;
                    case Op::BRAF:
                        if (auto jt = matchJumpTable(pc, *in)) {
                            for (uint32_t t : jt->targets) push(stack, t, pc);
                            out_.jump_tables[pc] = std::move(*jt);
                        } else if (auto v = constBefore(pc, in->rn, nullptr)) {
                            // braf com deslocamento constante: o alvo vira funcao (despacho em tempo de execucao)
                            addEntry(pc + 4 + *v, Origin::Call, pc);
                        }
                        break;
                    default: break;  // rts, rte
                    }
                    if (!cont) break;
                    pc = next;
                }
            }
        }
    }

    // Ponteiro de funcao plausivel: dentro da regiao de codigo, nao e dado, sem
    // instrucao privilegiada no caminho linear, e com cara de funcao do SHC
    // (prologo tipico) ou folha curta que termina em rts.
    static bool isPrologue(const Insn& in) {
        switch (in.op) {
        case Op::STSL_PR: case Op::STSL_MACL: case Op::STSL_MACH: return in.rn == 15;  // so na pilha
        case Op::MOVL_PREDEC: case Op::FMOVS_PREDEC: return in.rn == 15;
        case Op::ADD_IMM: return in.rn == 15 && in.imm < 0;
        default: return false;
        }
    }

    // Trecho linear com pelo menos `min` instrucoes validas (sem dado nem privilegiada)
    // ate um desvio incondicional com delay slot valido.
    // Privilegiadas comuns em rotinas de sistema do Katana com prologo: mascarar
    // interrupcoes (stc/ldc sr) e manipular o contexto de excecao (ssr/spc), como no
    // trampolim de retorno de interrupcao 8C17DE4C. As demais desqualificam candidatos.
    static bool kernelOp(const Insn& in) {
        switch (in.op) {
        case Op::STC_SR: case Op::LDC_SR: case Op::STC_SSR: case Op::LDC_SSR: case Op::STC_SPC: case Op::LDC_SPC:
        case Op::STCL_SSR: case Op::LDCL_SSR: case Op::STCL_SPC: case Op::LDCL_SPC:
            return true;
        default:
            return false;
        }
    }

    // Todo o corpo alcancavel a partir de `t` (desvios condicionais, bra, slots e alvos de
    // bsr) decodifica como codigo valido, fora de dados e sem privilegiadas alem de
    // stc/ldc sr. Valida candidatos vindos de ponteiros antes de virarem funcao: o
    // trecho linear sozinho aceitava dados (p.ex. 8C0AAAEC, 8C1DC228).
    bool bodyIsCode(uint32_t t) const {
        std::vector<uint32_t> stack{t};
        std::set<uint32_t>    seen;
        auto ok = [&](uint32_t pc, bool slot) {
            auto in = at(pc);
            if (!in || out_.data.count(pc)) return false;
            if (in->has(sh4::F_PRIV) && !kernelOp(*in)) return false;
            return !(slot && in->has(sh4::F_SLOT_ILLEGAL));
        };
        while (!stack.empty()) {
            uint32_t pc = stack.back();
            stack.pop_back();
            for (;;) {
                if (!seen.insert(pc).second) break;
                if (seen.size() > 8192 || !ok(pc, false)) return seen.size() > 8192;
                const Insn in = *at(pc);
                if (!in.has(sh4::F_BRANCH)) {
                    pc += 2;
                    continue;
                }
                if (in.has(sh4::F_DELAYED) && !ok(pc + 2, true)) return false;
                const uint32_t next = pc + (in.has(sh4::F_DELAYED) ? 4 : 2);
                switch (in.op) {
                case Op::BT: case Op::BF: case Op::BTS: case Op::BFS:
                    stack.push_back(sh4::target(in, pc));
                    pc = next;
                    continue;
                case Op::BRA:
                    stack.push_back(sh4::target(in, pc));
                    break;
                case Op::BSR:
                    if (!ok(sh4::target(in, pc), false)) return false;
                    pc = next;
                    continue;
                case Op::JSR: case Op::BSRF:
                    pc = next;
                    continue;
                default:  // rts, rte, jmp, braf
                    break;
                }
                break;
            }
        }
        return true;
    }

    // `srOk`: stc/ldc sr permitidas (mascarar interrupcoes, comum nas rotinas do Katana).
    bool plausibleCode(uint32_t t, uint32_t min, bool srOk = false) const {
        for (uint32_t pc = t, k = 0; k < 1024; ++k, pc += 2) {
            auto in = at(pc);
            const bool sr = in && kernelOp(*in);
            if (!in || out_.data.count(pc) || (in->has(sh4::F_PRIV) && !(srOk && sr))) return false;
            if (in->has(sh4::F_BRANCH) && !in->has(sh4::F_COND)) {
                auto slot = at(pc + 2);
                return k >= min && slot && !slot->has(sh4::F_SLOT_ILLEGAL) && !out_.data.count(pc + 2);
            }
        }
        return false;
    }

    bool looksLikeFunction(uint32_t t) const {
        if (visited_.empty() || t < *visited_.begin() || t > *visited_.rbegin()) return false;
        if (out_.data.count(t)) return false;
        auto first = at(t);
        if (!first) return false;
        // prologo nas 4 primeiras instrucoes, depois de preparo em linha reta
        // (p.ex. "sts fpscr,r0; mov.l r0,@-r15" em 8C1C56D4; "mov #0,r3; mov.l @(d,pc),r2;
        // mov.l r14,@-r15" em 8C17B04E, alcancada por tabela de ponteiros)
        bool prologue = false;
        for (uint32_t k = 0; k < 4 && !prologue; ++k) {
            auto in = at(t + 2 * k);
            if (!in || in->has(sh4::F_BRANCH)) break;
            prologue = isPrologue(*in);
        }
        for (uint32_t pc = t, k = 0; k < 1024; ++k, pc += 2) {
            auto in = at(pc);
            // stc/ldc sr (mascarar interrupcoes) sao comuns em rotinas do Katana com prologo
            // (p.ex. 8C17C1B8, callback de interrupcao); outras privilegiadas desqualificam
            // (dado com cara de codigo: ldc.l @r5+,spc em 8C105516)
            const bool srOnly = in && kernelOp(*in);
            if (!in || out_.data.count(pc) || (in->has(sh4::F_PRIV) && !(prologue && srOnly))) return false;
            if (in->has(sh4::F_BRANCH) && !in->has(sh4::F_COND)) {
                auto slot = at(pc + 2);
                if (!slot || slot->has(sh4::F_SLOT_ILLEGAL) || out_.data.count(pc + 2)) return false;
                if (prologue) return true;
                // stub/thunk: "bra comum; mov #k,r4" ou "mov.l @(d,PC),rX; jmp @rX"
                // (o destino precisa ser codigo ja conhecido, para nao aceitar dado com 0xAxxx)
                if (k < 4 && in->op == Op::BRA) {
                    const uint32_t d = sh4::target(*in, pc);
                    return visited_.count(d) > 0 || plausibleCode(d, 8);
                }
                if (k < 4 && in->op == Op::JMP) {
                    auto v = constBefore(pc, in->rn, nullptr);
                    const uint32_t d = v ? img_.normalize(*v) : 0;
                    return d && (visited_.count(d) || entries_.count(d));
                }
                // folha sem prologo: rts em ate 24 instrucoes; "rts" sozinho so com nop no slot
                return in->op == Op::RTS && k < 24 && (k > 0 || slot->op == Op::NOP);
            }
        }
        return false;
    }

    // Fronteira de funcao: logo depois de um desvio incondicional com delay slot,
    // de dado do pool ou de padding (nop).
    bool atBoundary(uint32_t t) const {
        if (!img_.contains(t - 4, 4)) return false;
        if (out_.data.count(t - 2)) return true;
        if (!at(t - 2)) return true;  // palavra anterior nao decodifica: pool de funcao ainda nao alcancada
        if (img_.read16(t - 2) == 0x0009 && t >= 6) {  // nop de alinhamento
            if (auto p = at(t - 6); p && p->has(sh4::F_DELAYED) && !p->has(sh4::F_COND) && !p->has(sh4::F_CALL)) return true;
            if (out_.data.count(t - 4)) return true;
        }
        auto br = at(t - 4);
        return br && br->has(sh4::F_DELAYED) && !br->has(sh4::F_COND) && !br->has(sh4::F_CALL);
    }

    // ---------- fase 2: corpo de cada funcao ----------
    void buildFunction(uint32_t entry, Origin origin) {
        Function f;
        f.entry  = entry;
        f.origin = origin;
        std::vector<std::pair<uint32_t, Insn>> indirect;
        std::vector<uint32_t>                  stack{entry};
        while (!stack.empty()) {
            uint32_t pc = stack.back();
            stack.pop_back();
            for (;;) {
                if (f.insns.count(pc)) break;
                auto in = at(pc);
                if (!in || out_.data.count(pc)) {
                    f.dead_ends.insert(pc);
                    break;
                }
                f.insns.insert(pc);
                if (!in->has(sh4::F_BRANCH)) {
                    pc += 2;
                    continue;
                }
                if (in->has(sh4::F_DELAYED)) {
                    auto slot = at(pc + 2);
                    if (!slot || slot->has(sh4::F_SLOT_ILLEGAL) || slot->has(sh4::F_PCREL)) {
                        f.dead_ends.insert(pc + 2);
                        break;
                    }
                    f.slots.insert(pc + 2);
                }
                const uint32_t next = pc + (in->has(sh4::F_DELAYED) ? 4 : 2);
                auto           go   = [&](uint32_t t) {
                    f.labels.insert(t);
                    stack.push_back(t);
                };
                bool cont = false;
                switch (in->op) {
                case Op::BT: case Op::BF: case Op::BTS: case Op::BFS:
                    go(sh4::target(*in, pc));
                    cont = true;
                    break;
                case Op::BRA: {
                    const uint32_t t = sh4::target(*in, pc);
                    if (t != entry && entries_.count(t)) f.tail_calls.insert(pc);
                    else go(t);
                    break;
                }
                case Op::BSR: cont = true; break;
                case Op::JSR: case Op::BSRF:
                    indirect.push_back({pc, *in});
                    cont = true;
                    break;
                case Op::JMP: case Op::BRAF:
                    if (auto it = out_.jump_tables.find(pc); it != out_.jump_tables.end()) {
                        for (uint32_t t : it->second.targets) go(t);
                    } else {
                        indirect.push_back({pc, *in});
                    }
                    break;
                default: break;
                }
                if (!cont) break;
                pc = next;
            }
        }
        // Resolucao segura (sem cruzar labels) dos desvios indiretos.
        for (auto& [pc, in] : indirect) {
            std::optional<uint32_t> v = constBefore(pc, in.rn, &f.labels);
            uint32_t                t = 0;
            if (v) t = img_.normalize(in.op == Op::BSRF || in.op == Op::BRAF ? pc + 4 + *v : *v);
            if (t && entries_.count(t)) {
                f.direct[pc] = t;
                if (in.op == Op::JMP) f.tail_calls.insert(pc);
            } else {
                f.unresolved.insert(pc);
            }
        }
        out_.functions.emplace(entry, std::move(f));
    }

    // Invariante: todo codigo alcancado na fase 1 pertence a alguma funcao da fase 2.
    void checkOrphans() {
        std::set<uint32_t> covered;
        for (auto& [a, f] : out_.functions) {
            covered.insert(f.insns.begin(), f.insns.end());
            covered.insert(f.slots.begin(), f.slots.end());
        }
        size_t n = 0;
        for (uint32_t pc : visited_) {
            if (covered.count(pc)) continue;
            if (++n <= 20) {
                // sobe ate o inicio do trecho orfao para achar quem o alcancou
                uint32_t s = pc;
                while (visited_.count(s - 2) && !covered.count(s - 2)) s -= 2;
                auto it = reachedFrom_.find(s);
                warn("codigo orfao (fase 1 sem funcao) em " + hex(pc) + ", trecho desde " + hex(s) +
                     (it != reachedFrom_.end() ? ", alcancado a partir de " + hex(it->second) : ""));
            }
        }
        out_.stats.orphans = n;
    }

    void computeStats() {
        Stats& s      = out_.stats;
        s.functions   = out_.functions.size();
        s.unique_instructions = visited_.size();
        s.pool_words  = out_.data.size();
        for (auto& [a, f] : out_.functions) {
            s.by_call += f.origin == Origin::Call;
            s.by_pointer += f.origin == Origin::Pointer;
            s.by_data_pointer += f.origin == Origin::DataPointer;
            s.by_config += f.origin == Origin::Config;
            s.instructions += f.insns.size() + f.slots.size();
            s.direct_indirects += f.direct.size();
            s.unresolved_indirects += f.unresolved.size();
            s.dead_ends += f.dead_ends.size();
        }
        for (auto& [pc, jt] : out_.jump_tables) {
            ++s.jump_tables;
            s.jump_tables_heuristic += !jt.bounded;
        }
    }

    const Image&                 img_;
    const Config&                cfg_;
    Analysis                     out_;
    std::map<uint32_t, Origin>   entries_;
    std::vector<uint32_t>        pending_;
    std::set<uint32_t>           visited_;
    std::set<uint32_t>           poolValues_;
    std::set<uint32_t>           external_;
    std::map<uint32_t, uint32_t> reachedFrom_;
};

} // namespace

Analysis analyze(const Image& img, const Config& cfg) { return Analyzer(img, cfg).run(); }

} // namespace bs::analysis
