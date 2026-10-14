#include "codegen/codegen.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace bs::codegen {
namespace fs = std::filesystem;
using analysis::Analysis;
using analysis::Function;
using analysis::Image;
using sh4::Insn;
using sh4::Op;

namespace {

std::string hx(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%08Xu", v);
    return b;
}
std::string fname(uint32_t a) {
    char b[16];
    std::snprintf(b, sizeof(b), "f_%08X", a);
    return b;
}
std::string lname(uint32_t a) {
    char b[16];
    std::snprintf(b, sizeof(b), "L_%08X", a);
    return b;
}

// Pontos de reentrada de `f`: retorno de cada chamada a setjmp (alvo resolvido).
std::set<uint32_t> reentryPoints(const Image& img, const Function& f, const Options& opt) {
    std::set<uint32_t> out;
    if (opt.setjmp_funcs.empty()) return out;
    for (uint32_t pc : f.insns) {
        const Insn in = sh4::decode(img.read16(pc));
        uint32_t   t  = 0;
        if (in.op == Op::BSR) t = img.normalize(sh4::target(in, pc));
        else if (in.op == Op::JSR || in.op == Op::BSRF)
            if (auto it = f.direct.find(pc); it != f.direct.end()) t = it->second;
        if (t && opt.setjmp_funcs.count(t) && f.insns.count(pc + 4)) out.insert(pc + 4);
    }
    return out;
}

class FnEmitter {
public:
    FnEmitter(const Image& img, const Analysis& a, const Function& f, const Options& opt)
        : img_(img), a_(a), f_(f), opt_(opt) {}

    std::string run() {
        std::vector<uint32_t> order(f_.insns.begin(), f_.insns.end());
        reentry_ = reentryPoints(img_, f_, opt_);
        labels_.insert(reentry_.begin(), reentry_.end());
        // Inicio de bloco: entrada, labels e o que vem depois de um desvio.
        std::set<uint32_t> blockStart(f_.labels.begin(), f_.labels.end());
        blockStart.insert(f_.entry);
        for (uint32_t pc : order) {
            const Insn in = at(pc);
            if (in.has(sh4::F_BRANCH)) blockStart.insert(pc + (in.has(sh4::F_DELAYED) ? 4 : 2));
        }

        std::vector<std::string> bodies(order.size());
        for (size_t k = 0; k < order.size(); ++k) {
            const uint32_t pc   = order[k];
            const uint32_t next = k + 1 < order.size() ? order[k + 1] : 0;
            std::string&   s    = bodies[k];
            if (blockStart.count(pc)) {
                uint32_t n = 0;
                for (size_t j = k; j < order.size(); ++j) {
                    if (j > k && blockStart.count(order[j])) break;
                    const Insn in = at(order[j]);
                    n += in.has(sh4::F_DELAYED) ? 2 : 1;
                    if (in.has(sh4::F_BRANCH)) break;
                }
                s += "    BS_CYCLES(" + std::to_string(n) + ");\n";
            }
            emitInsn(s, pc, next);
        }

        std::string out = "void " + fname(f_.entry) + "(bs::Sh4Context* c) {\n    BS_FN_ENTER(" + hx(f_.entry) + ");\n";
        if (order.empty()) {
            out += "    ::bs::sh4_unreachable(c, " + hx(f_.entry) + ");\n}\n";
            return out;
        }
        if (img_.relocatedAt(f_.entry)) out += codeGuard();
        if (order.front() != f_.entry) {
            labels_.insert(f_.entry);
            out += "    goto " + lname(f_.entry) + ";\n";
        }
        for (size_t k = 0; k < order.size(); ++k) {
            if (labels_.count(order[k])) out += lname(order[k]) + ":;\n";
            out += bodies[k];
        }
        // longjmp de volta a este quadro (BS_CALL_REENTRANT, R15 igual ao da chamada). Alvo
        // que nao e desta funcao: outro quadro com o mesmo R15 mais acima; o redirect
        // continua pendente e a funcao retorna (revisao Codex 2026-10-14).
        if (!reentry_.empty()) {
            out += "    return;\nbs_reenter_:;\n    switch (c->redirect) {\n";
            for (uint32_t r : reentry_) out += "    case " + hx(r) + ": c->redirect = 0; goto " + lname(r) + ";\n";
            out += "    default: return;\n    }\n";
        }
        out += "}\n";
        return out;
    }

private:
    Insn at(uint32_t pc) const { return sh4::decode(img_.read16(pc)); }

    // Codigo copiado pelo jogo para a RAM (Image::relocated): antes de executar, confere
    // que as instrucoes la sao as compiladas (o pool, que e dado, fica de fora).
    std::string codeGuard() const {
        std::set<uint32_t> pcs(f_.insns.begin(), f_.insns.end());
        pcs.insert(f_.slots.begin(), f_.slots.end());
        std::string a = "    static const uint32_t kGuardPc[] = {", o = "    static const uint16_t kGuardOp[] = {";
        for (uint32_t pc : pcs) {
            a += hx(pc) + ",";
            o += hx(img_.read16(pc)) + ",";
        }
        return a + "};\n" + o + "};\n    ::bs::sh4_check_code(c, kGuardPc, kGuardOp, " + std::to_string(pcs.size()) +
               ");\n";
    }

    // Transferencia para `t`: goto se `t` esta no corpo, senao fim morto explicito.
    std::string jumpTo(uint32_t t, uint32_t from) {
        if (!f_.insns.count(t)) return "{ ::bs::sh4_unreachable(c, " + hx(t) + "); return; }";
        labels_.insert(t);
        return std::string(t <= from ? "{ BS_POLL(" + hx(t) + "); goto " : "{ goto ") + lname(t) + "; }";
    }

    void fallthrough(std::string& s, uint32_t pc, uint32_t cont, uint32_t next) {
        if (cont != next) s += "    " + jumpTo(cont, pc) + "\n";
    }

    void comment(std::string& s, uint32_t pc, const Insn& in, const char* indent = "    ") {
        if (!opt_.comments) return;
        char b[24];
        std::snprintf(b, sizeof(b), "// %08X: ", pc);
        s += indent + std::string(b) + sh4::disasm(in, pc) + "\n";
    }

    // Instrucao sem efeito de controle de fluxo (tambem usada para delay slots).
    std::string plain(uint32_t pc, const Insn& in) const {
        char        b[160];
        const char* name = in.info().name;
        switch (in.op) {
        case Op::Invalid:  // p.ex. slot de desvio que cai em dado (fim morto da analise)
            std::snprintf(b, sizeof(b), "::bs::sh4_illegal(c, 0x%08Xu, 0x%04Xu);", pc, unsigned(img_.read16(pc)));
            return b;
        case Op::MOVW_PC: case Op::MOVL_PC: {
            const uint32_t addr = sh4::target(in, pc);
            const bool     word = in.op == Op::MOVW_PC;
            // pool de codigo realocado e lido da RAM: o jogo pode corrigi-lo na copia
            if (opt_.inline_pool && img_.inMain(addr, word ? 2 : 4)) {
                const uint32_t v = word ? uint32_t(int32_t(int16_t(img_.read16(addr)))) : img_.read32(addr);
                std::snprintf(b, sizeof(b), "c->r[%u] = 0x%08Xu;", in.rn, v);
            } else {
                std::snprintf(b, sizeof(b), "bs::op::%s(c, %u, 0, 0x%08Xu);", name, in.rn, addr);
            }
            return b;
        }
        case Op::MOVA:
            std::snprintf(b, sizeof(b), "c->r[0] = 0x%08Xu;", sh4::target(in, pc));
            return b;
        case Op::TRAPA: case Op::SLEEP: case Op::LDTLB:
            std::snprintf(b, sizeof(b), "c->pc = 0x%08Xu; bs::op::%s(c, %u, %u, 0x%Xu);", pc, name, in.rn, in.rm,
                          uint32_t(in.imm));
            return b;
        default:
            std::snprintf(b, sizeof(b), "bs::op::%s(c, %u, %u, 0x%Xu);", name, in.rn, in.rm, uint32_t(in.imm));
            return b;
        }
    }

    std::string slot(uint32_t pc) const {
        const Insn in = at(pc + 2);
        std::string s = plain(pc + 2, in);
        if (opt_.comments) s += "  // slot: " + sh4::disasm(in, pc + 2);
        return s;
    }

    std::string callMacro() const { return reentry_.empty() ? "BS_CALL(" : "BS_CALL_REENTRANT("; }

    std::string callTo(uint32_t pc, uint32_t t, uint32_t ret) const {
        const uint32_t nt = img_.normalize(t);
        if (nt && a_.functions.count(nt)) return callMacro() + fname(nt) + "(c), " + hx(ret) + ");";
        return callMacro() + "::bs::sh4_dispatch(c, " + hx(pc) + ", " + hx(t) + "), " + hx(ret) + ");";
    }

    void emitInsn(std::string& s, uint32_t pc, uint32_t next) {
        const Insn in = at(pc);
        comment(s, pc, in);
        if (!in.has(sh4::F_BRANCH)) {
            s += "    " + plain(pc, in) + "\n";
            // SR novo pode desmascarar uma interrupcao pendente: o hardware a aceita antes
            // da proxima instrucao (o Flycast faz o mesmo; ver STATUS.md, checkpoint 8C094E28)
            if (in.op == Op::LDC_SR || in.op == Op::LDCL_SR) s += "    BS_IRQ_CHECK(" + hx(pc + 2) + ");\n";
            // pref numa store queue pode enviar um fim de lista ao TA, que gera interrupcao
            // na hora (no Flycast, aceita poucas instrucoes depois; ver STATUS.md, fim de
            // lista opaca em 8C18BC3C): checar aqui, nao so no proximo poll
            if (in.op == Op::PREF) s += "    BS_IRQ_CHECK(" + hx(pc + 2) + ");\n";
            fallthrough(s, pc, pc + 2, next);
            return;
        }
        const uint32_t ret = pc + 4;
        switch (in.op) {
        case Op::BT: case Op::BF:
            s += std::string("    if (") + (in.op == Op::BT ? "c->t" : "!c->t") + ") " + jumpTo(sh4::target(in, pc), pc) + "\n";
            fallthrough(s, pc, pc + 2, next);
            return;
        case Op::BTS: case Op::BFS:
            s += "    { const uint32_t take = c->t; " + slot(pc) + "\n";
            s += std::string("      if (") + (in.op == Op::BTS ? "take" : "!take") + ") " + jumpTo(sh4::target(in, pc), pc) + " }\n";
            fallthrough(s, pc, pc + 4, next);
            return;
        case Op::BRA: {
            const uint32_t t = sh4::target(in, pc);
            s += "    " + slot(pc) + "\n";
            if (f_.tail_calls.count(pc)) s += "    " + fname(t) + "(c); return;\n";
            else s += "    " + jumpTo(t, pc) + "\n";
            return;
        }
        case Op::BSR:
            s += "    c->pr = " + hx(ret) + "; " + slot(pc) + "\n";
            s += "    " + callTo(pc, sh4::target(in, pc), ret) + "\n";
            fallthrough(s, pc, ret, next);
            return;
        case Op::JSR: case Op::BSRF: {
            const std::string tgt = in.op == Op::JSR ? "c->r[" + std::to_string(in.rn) + "]"
                                                     : hx(ret) + " + c->r[" + std::to_string(in.rn) + "]";
            s += "    { const uint32_t tgt = " + tgt + "; c->pr = " + hx(ret) + "; " + slot(pc) + "\n";
            if (auto it = f_.direct.find(pc); it != f_.direct.end()) {
                s += "      BS_EXPECT_TARGET(" + hx(pc) + ", tgt, " + hx(it->second) + ");\n";
                s += "      " + callMacro() + fname(it->second) + "(c), " + hx(ret) + "); }\n";
            } else {
                s += "      " + callMacro() + "::bs::sh4_dispatch(c, " + hx(pc) + ", tgt), " + hx(ret) + "); }\n";
            }
            fallthrough(s, pc, ret, next);
            return;
        }
        case Op::JMP: case Op::BRAF: {
            const std::string tgt = in.op == Op::JMP ? "c->r[" + std::to_string(in.rn) + "]"
                                                     : hx(ret) + " + c->r[" + std::to_string(in.rn) + "]";
            s += "    { const uint32_t tgt = " + tgt + "; " + slot(pc) + "\n";
            if (auto jt = a_.jump_tables.find(pc); jt != a_.jump_tables.end()) {
                s += "      switch (::bs::sh4_phys(tgt)) {\n";
                std::set<uint32_t> seen;
                for (uint32_t t : jt->second.targets)
                    if (seen.insert(t).second)
                        s += "      case " + hx(t & 0x1FFFFFFF) + ": " + jumpTo(t, pc) + "\n";
                s += "      default: ::bs::sh4_dispatch(c, " + hx(pc) + ", tgt); return;\n      } }\n";
            } else if (auto it = f_.direct.find(pc); it != f_.direct.end()) {
                s += "      BS_EXPECT_TARGET(" + hx(pc) + ", tgt, " + hx(it->second) + ");\n";
                s += "      " + fname(it->second) + "(c); return; }\n";
            } else {
                s += "      ::bs::sh4_dispatch(c, " + hx(pc) + ", tgt); return; }\n";
            }
            return;
        }
        case Op::RTS:
            s += "    { const uint32_t tgt = c->pr; " + slot(pc) + "\n      " +
                 (opt_.longjmp_funcs.count(f_.entry) ? "BS_LONGJMP_RET(" : "BS_RET(") + hx(pc) + ", tgt); }\n";
            return;
        case Op::RTE:
            // SPC e lido antes; SR = SSR antes do slot (o slot ja ve o banco restaurado).
            s += "    { const uint32_t tgt = c->spc; ::bs::sh4_set_sr(*c, c->ssr); " + slot(pc) + "\n      BS_RTE(" +
                 hx(pc) + ", tgt); }\n";
            return;
        default:
            throw std::logic_error("desvio sem traducao");
        }
    }

    const Image&       img_;
    const Analysis&    a_;
    const Function&    f_;
    const Options&     opt_;
    std::set<uint32_t> labels_;
    std::set<uint32_t> reentry_;  // retorno de chamadas a setjmp
};

void writeIfChanged(const fs::path& p, const std::string& text) {
    {
        std::ifstream in(p, std::ios::binary);
        if (in) {
            std::string old((std::istreambuf_iterator<char>(in)), {});
            if (old == text) return;  // preserva o timestamp: so recompila o que mudou
        }
    }
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) throw std::runtime_error("falha ao gravar " + p.string());
}

} // namespace

std::string emitFunction(const Image& img, const Analysis& a, const Function& f, const Options& opt) {
    return FnEmitter(img, a, f, opt).run();
}

Result generate(const Image& img, const Analysis& a, const Options& opt) {
    Result r;
    fs::create_directories(opt.out_dir);
    const char* banner = "// GERADO por bs-recomp a partir do executavel do usuario. NAO VERSIONAR.\n";

    std::string hdr = std::string(banner) + "#pragma once\n#include \"cpu/sh4_gen.h\"\n\nnamespace bsg {\n";
    for (const auto& [addr, f] : a.functions) hdr += "void " + fname(addr) + "(bs::Sh4Context* c);\n";
    hdr += "void register_functions();\n} // namespace bsg\n";
    writeIfChanged(opt.out_dir / "funcs.h", hdr);

    std::vector<std::string> sources;
    std::string              body;
    size_t                   inFile = 0;
    auto                     flush  = [&] {
        if (!inFile) return;
        char nm[32];
        std::snprintf(nm, sizeof(nm), "funcs_%03zu.cpp", sources.size());
        const std::string text = std::string(banner) + "#include \"funcs.h\"\n\nnamespace bsg {\n\n" + body + "} // namespace bsg\n";
        r.lines += size_t(std::count(text.begin(), text.end(), '\n'));
        writeIfChanged(opt.out_dir / nm, text);
        sources.push_back(nm);
        body.clear();
        inFile = 0;
    };
    for (const auto& [addr, f] : a.functions) {
        body += emitFunction(img, a, f, opt) + "\n";
        ++r.functions;
        if (++inFile >= opt.funcs_per_file) flush();
    }
    flush();

    // Tabela de despacho ordenada por endereco fisico.
    std::vector<std::pair<uint32_t, uint32_t>> table;
    for (const auto& [addr, f] : a.functions) table.push_back({addr & 0x1FFFFFFF, addr});
    std::sort(table.begin(), table.end());
    std::string d = std::string(banner) + "#include \"funcs.h\"\n\nnamespace bsg {\nnamespace {\nconst bs::Sh4FuncEntry kTable[] = {\n";
    for (auto [phys, addr] : table) d += "    {" + hx(phys) + ", &" + fname(addr) + "},\n";
    // Pontos de reentrada (retornos de setjmp): o runtime so retoma uma thread por
    // longjmp num deles, e so cria thread nova fora deles.
    std::set<uint32_t> reentry;
    for (const auto& [addr, f] : a.functions) {
        const auto pts = reentryPoints(img, f, opt);
        reentry.insert(pts.begin(), pts.end());
    }
    d += "};\nconst uint32_t kReentry[] = {";
    for (uint32_t p : reentry) d += hx(p) + ", ";
    d += "0};\n} // namespace\n\nvoid register_functions() {\n    bs::sh4_set_function_table(kTable, sizeof(kTable) / sizeof(kTable[0]));\n"
         "    bs::sh4_set_reentry_points(kReentry, " + std::to_string(reentry.size()) + ");\n}\n} // namespace bsg\n";
    writeIfChanged(opt.out_dir / "dispatch.cpp", d);
    sources.push_back("dispatch.cpp");

    std::string cm = "# GERADO por bs-recomp. NAO VERSIONAR.\nset(BS_GENERATED_SOURCES\n";
    for (const auto& s : sources) cm += "  ${CMAKE_CURRENT_LIST_DIR}/" + s + "\n";
    cm += ")\n";
    writeIfChanged(opt.out_dir / "generated.cmake", cm);
    r.files = sources.size();
    return r;
}

} // namespace bs::codegen
