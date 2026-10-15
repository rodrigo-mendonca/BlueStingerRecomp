// bs-hwscan: manifesto das dependencias de hardware do Dreamcast, bibliotecas do SDK,
// fronteira jogo/SDK, fechamento e metricas (N0, docs/N0-RELATORIO.md).
//
//   bs-hwscan <config.toml> <1ST_READ.BIN> <saida>
//             [--fronteira <toml>]          confere o fechamento contra a fronteira declarada
//             [--fronteira-gerar <toml>]    grava a fronteira derivada do grafo atual
//             [--metricas-base <toml>]      confere a catraca contra a linha de base
//             [--metricas-gerar <toml>]     grava as metricas atuais como linha de base
//             [--sonda <tsv>]               junta o TSV da sonda dinamica (BS_HW_PROBE)
//             [--gerado <dir>]              metricas do codigo gerado (generated/)
//             [--runtime <dir>]             leituras do contador de ciclos no runtime (N2a)
// Saida: 0 ok; 1 erro; 2 uso; 3 violacao de fechamento; 4 regressao de metrica.
#include "hwscan/boundary_io.h"
#include "hwscan/pipeline.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace bs;
using namespace bs::hwscan;

namespace {

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

void writeText(const fs::path& p, const std::string& text) {
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << text;
    if (!o) throw std::runtime_error("falha ao gravar " + p.filename().string());
}

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("nao foi possivel abrir " + p.filename().string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeScan(const Scan& s, const fs::path& dir) {
    {
        std::ofstream o(dir / "hw_manifest.tsv", std::ios::trunc);
        o << "funcao\tpc\tendereco\ttamanho\ttipo\tregiao\tsubsistema\torigem\tcontagem\n";
        for (const auto& r : s.refs)
            o << hex(r.function) << '\t' << hex(r.pc) << '\t' << hex(r.address) << '\t' << int(r.size) << '\t'
              << kind_name(r.kind) << '\t' << dcmap::region_name(r.region) << '\t'
              << dcmap::subsystem_name(dcmap::region_subsystem(r.region)) << '\t'
              << (r.dynamic ? "sonda" : "estatica") << '\t' << r.count << '\n';
    }
    {
        std::ofstream o(dir / "callgraph.tsv", std::ios::trunc);
        o << "chamador\tchamado\tpc\ttipo\n";
        static const char* kEdge[] = {"chamada", "tail", "indireto", "dinamica"};
        for (const auto& e : s.edges)
            o << hex(e.caller) << '\t' << (e.callee ? hex(e.callee) : std::string("-")) << '\t' << hex(e.pc) << '\t'
              << kEdge[int(e.kind)] << '\n';
    }
    {
        std::ofstream o(dir / "strings.tsv", std::ios::trunc);
        o << "funcao\tpc\tendereco\tstring\n";
        for (const auto& [fn, list] : s.strings)
            for (const auto& str : list)
                o << hex(fn) << '\t' << hex(str.pc) << '\t' << hex(str.address) << '\t' << str.text << '\n';
    }
    {
        std::map<uint32_t, std::map<std::string, int>> per;
        for (const auto& r : s.refs) per[r.function][dcmap::region_name(r.region)]++;
        std::ofstream o(dir / "funcoes_hw.tsv", std::ios::trunc);
        o << "funcao\tacessos\tregioes\n";
        for (const auto& [fn, regs] : per) {
            int         n = 0;
            std::string list;
            for (const auto& [name, c] : regs) {
                n += c;
                list += (list.empty() ? "" : ",") + name + ":" + std::to_string(c);
            }
            o << hex(fn) << '\t' << n << '\t' << list << '\n';
        }
    }
}

void writeSdk(const SdkId& id, const fs::path& dir) {
    std::ofstream o(dir / "sdk_funcs.tsv", std::ios::trunc);
    o << "funcao\tcamada\tfamilia\tconfianca\tconflito\tnome\tevidencias\n";
    for (const auto& [fn, f] : id.functions) {
        std::string ev;
        for (const auto& e : f.evidence) ev += (ev.empty() ? "" : ",") + e;
        o << hex(fn) << '\t' << layer_name(f.layer) << '\t' << family_name(f.family) << '\t'
          << confidence_name(f.confidence) << '\t' << (f.conflict ? "sim" : "nao") << '\t'
          << (f.name.empty() ? "-" : f.name) << '\t' << (ev.empty() ? "-" : ev) << '\n';
    }
    // faixas contiguas (em ordem de endereco) da mesma camada e familia
    std::ofstream r(dir / "sdk_faixas.tsv", std::ios::trunc);
    r << "inicio\tfim\tcamada\tfamilia\tfuncoes\n";
    uint32_t start = 0, last = 0, n = 0;
    Layer    curL = Layer::Unknown;
    Family   curF = Family::Unknown;
    auto     flush = [&] {
        if (n) r << hex(start) << '\t' << hex(last) << '\t' << layer_name(curL) << '\t' << family_name(curF) << '\t' << n << '\n';
    };
    for (const auto& [fn, f] : id.functions) {
        if (!n || f.family != curF || f.layer != curL) {
            flush();
            start = fn, curL = f.layer, curF = f.family, n = 0;
        }
        last = fn, ++n;
    }
    flush();
    std::ofstream m(dir / "sdk_modulos.tsv", std::ios::trunc);
    m << "banner\tdados_inicio\tdados_fim\tmodulo\tfamilia\tlimitado\ttexto\n";
    for (const auto& mod : id.modules)
        m << hex(mod.banner) << '\t' << hex(mod.dataBegin) << '\t' << hex(mod.dataEnd) << '\t' << mod.name << '\t'
          << family_name(mod.family) << '\t' << (mod.bounded ? "sim" : "nao") << '\t' << mod.text << '\n';
    std::ofstream b(dir / "blocos.tsv", std::ios::trunc);
    b << "primeira\tultima\ttrocas_jogo_sdk\n";
    for (const auto& blk : id.blocks) b << hex(blk.first) << '\t' << hex(blk.last) << '\t' << blk.switches << '\n';
}

void writePrefSq(const Scan& s, const SdkId& id, const fs::path& dir) {
    std::set<uint32_t> fns;
    for (const auto& [fn, n] : s.prefUnresolved) fns.insert(fn);
    for (const auto& [fn, n] : s.sqLiteral) fns.insert(fn);
    std::ofstream o(dir / "pref_sq.tsv", std::ios::trunc);
    o << "funcao\tcamada\tfamilia\tpref_nao_resolvido\tliteral_e0000000\n";
    for (uint32_t fn : fns) {
        const auto it = id.functions.find(fn);
        const auto p  = s.prefUnresolved.find(fn);
        const auto q  = s.sqLiteral.find(fn);
        o << hex(fn) << '\t' << layer_name(it == id.functions.end() ? Layer::Unknown : it->second.layer) << '\t'
          << family_name(it == id.functions.end() ? Family::Unknown : it->second.family) << '\t'
          << (p == s.prefUnresolved.end() ? 0 : p->second) << '\t' << (q == s.sqLiteral.end() ? 0 : q->second) << '\n';
    }
}

void writeBoundary(const std::map<uint32_t, BoundaryEntry>& bd, const fs::path& dir) {
    std::ofstream o(dir / "fronteira.tsv", std::ios::trunc);
    o << "funcao\ttipo\tfamilia\tsubsistemas\tchamadas_do_jogo\tnome\n";
    for (const auto& [addr, e] : bd) {
        std::string subs;
        for (auto s : e.subsystems) subs += (subs.empty() ? "" : ",") + std::string(dcmap::subsystem_name(s));
        o << hex(addr) << '\t' << boundary_kind_name(e.kind) << '\t' << family_name(e.family) << '\t'
          << (subs.empty() ? "-" : subs) << '\t' << e.callSites << '\t' << (e.name.empty() ? "-" : e.name) << '\n';
    }
}

void writeDominance(const Scan& s, const SdkId& id, const std::set<uint32_t>& boundary, const fs::path& dir) {
    const auto dom = hw_dominance(s, id, boundary);
    std::map<uint32_t, std::set<std::string>> regions;
    for (const auto& r : s.refs)
        if (r.function) regions[r.function].insert(dcmap::region_name(r.region));
    std::ofstream o(dir / "hw_dominancia.tsv", std::ios::trunc);
    o << "funcao\tcamada\tfamilia\tregioes\tfronteiras\texemplos\n";
    for (const auto& [fn, bs] : dom) {
        const auto  it = id.functions.find(fn);
        std::string regs, ex;
        for (const auto& r : regions[fn]) regs += (regs.empty() ? "" : ",") + r;
        int k = 0;
        for (uint32_t b : bs) {
            if (k++ == 5) break;
            ex += (ex.empty() ? "" : ",") + hex(b);
        }
        o << hex(fn) << '\t' << layer_name(it == id.functions.end() ? Layer::Unknown : it->second.layer) << '\t'
          << family_name(it == id.functions.end() ? Family::Unknown : it->second.family) << '\t' << regs << '\t'
          << bs.size() << '\t' << (ex.empty() ? "runtime" : ex) << '\n';
    }
}

// Metricas do codigo gerado (PLANO-PORT-NATIVO.md 1.1): ocorrencias no texto dos arquivos.
Metrics generatedMetrics(const fs::path& dir) {
    Metrics m;
    const std::pair<const char*, const char*> kTokens[] = {
        {"gerado.bs_call", "BS_CALL("},       {"gerado.bs_call_reentrant", "BS_CALL_REENTRANT("},
        {"gerado.bs_poll", "BS_POLL("},       {"gerado.sh4_dispatch", "sh4_dispatch("},
        {"gerado.ops_fpu", "bs::op::F"},     {"gerado.bs_fn_enter", "BS_FN_ENTER("},
    };
    for (const auto& [k, tok] : kTokens) m[k] = 0;
    m["gerado.arquivos"] = 0;
    m["gerado.linhas"]   = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string name = e.path().filename().string();
        if (e.path().extension() != ".cpp" || (name.rfind("funcs_", 0) != 0 && name != "dispatch.cpp")) continue;
        const std::string text = readText(e.path());
        m["gerado.arquivos"]++;
        for (char ch : text) m["gerado.linhas"] += ch == '\n';
        for (const auto& [k, tok] : kTokens)
            for (size_t p = text.find(tok); p != std::string::npos; p = text.find(tok, p + 1)) m[k]++;
    }
    return m;
}

// N2a (criterio A8): quem le o contador de ciclos estimados do SH-4 (`->cycles`/`.cycles`) no
// runtime. Categorias: entrada de compatibilidade (cpu/time_hooks.h, time_now), base de
// progresso (BS_CYCLES/BS_POLL: cpu/sh4_gen.h e o poll central), diagnostico (debug/ e
// comandos de log/trace) e o resto, que e consumo funcional de tempo por ciclos (catraca).
Metrics runtimeMetrics(const fs::path& dir, const fs::path& tsv) {
    Metrics m;
    for (const char* k : {"tempo.consumidores.ciclos", "tempo.ciclos.funcionais", "tempo.ciclos.diagnostico",
                          "tempo.ciclos.progresso", "tempo.ciclos.entrada_compat"})
        m[k] = 0;
    std::ofstream out(tsv, std::ios::trunc);
    out << "arquivo\tlinha\tcategoria\n";
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(dir))
        if (e.is_regular_file() && (e.path().extension() == ".cpp" || e.path().extension() == ".h"))
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        const std::string rel = fs::relative(path, dir).generic_string();
        std::string       text = readText(path);
        // comentarios viram espacos (posicoes e linhas preservadas)
        for (size_t i = 0; i + 1 < text.size(); ++i) {
            if (text[i] == '"') {  // literal: pula
                for (++i; i < text.size() && text[i] != '"' && text[i] != '\n'; ++i)
                    if (text[i] == '\\') ++i;
            } else if (text[i] == '/' && text[i + 1] == '/') {
                for (; i < text.size() && text[i] != '\n'; ++i) text[i] = ' ';
            } else if (text[i] == '/' && text[i + 1] == '*') {
                for (; i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'); ++i)
                    if (text[i] != '\n') text[i] = ' ';
                if (i + 1 < text.size()) text[i] = text[i + 1] = ' ';
            }
        }
        bool functionalFile = false;
        for (size_t p = text.find("cycles"); p != std::string::npos; p = text.find("cycles", p + 1)) {
            const bool member = (p >= 2 && text.compare(p - 2, 2, "->") == 0) || (p >= 1 && text[p - 1] == '.');
            const bool word   = p + 6 >= text.size() || !(std::isalnum(uint8_t(text[p + 6])) || text[p + 6] == '_');
            if (!member || !word) continue;
            // comando que contem a leitura: do ultimo ; { } ate o proximo ; (ou o fim da linha,
            // nas macros)
            const size_t      b    = text.find_last_of(";{}", p);
            const size_t      e    = text.find_first_of(";\n", p);
            const std::string stmt = text.substr(b == std::string::npos ? 0 : b, e == std::string::npos ? std::string::npos : e - b);
            const size_t      ls   = text.rfind('\n', p);
            const std::string line = text.substr(ls == std::string::npos ? 0 : ls + 1, text.find('\n', p) - (ls == std::string::npos ? 0 : ls + 1));
            std::string cat;
            // so as expressoes conhecidas contam como entrada de compatibilidade e progresso; outra
            // leitura nesses arquivos e consumo funcional (revisao Codex 2026-10-15)
            if (rel == "cpu/time_hooks.h" && stmt.find("clock(c->cycles)") != std::string::npos) cat = "entrada_compat";
            else if (rel == "cpu/sh4_gen.h" &&
                     (line.find("#define BS_CYCLES(n) (c->cycles += (n))") != std::string::npos ||
                      line.find("if (c->cycles >= c->next_event)") != std::string::npos))
                cat = "progresso";
            else if (rel == "cpu/sh4_runtime.cpp" && stmt.find("c->next_event = c->cycles + kServiceQuantum") != std::string::npos)
                cat = "progresso";
            else if (rel.rfind("debug/", 0) == 0) cat = "diagnostico";
            else if (stmt.find("printf") != std::string::npos || stmt.find("event_trace(") != std::string::npos)
                cat = "diagnostico";
            else cat = "funcional";
            if (cat == "funcional") {
                functionalFile = true;
                m["tempo.ciclos.funcionais"]++;
            } else m["tempo.ciclos." + cat]++;
            out << rel << '\t' << 1 + std::count(text.begin(), text.begin() + p, '\n') << '\t' << cat << '\n';
        }
        if (functionalFile) m["tempo.consumidores.ciclos"]++;
    }
    return m;
}

void writeClosure(const ClosureResult& c, const fs::path& dir) {
    std::ofstream o(dir / "fechamento.tsv", std::ios::trunc);
    o << "tipo\tfuncao\talcancada_por\tvia_indireta\n";
    for (const auto& v : c.violations)
        o << (v.kind == Violation::SdkBypass ? "contorna_fronteira" : "acesso_hw") << '\t' << hex(v.function) << '\t'
          << hex(v.from) << '\t' << (v.viaIndirect ? "sim" : "nao") << '\n';
    for (uint32_t fn : c.unresolved) o << "indireto_nao_resolvido\t" << hex(fn) << "\t-\tsim\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "uso: bs-hwscan <config.toml> <1ST_READ.BIN> <saida> [--fronteira <toml>] "
                     "[--fronteira-gerar <toml>] [--metricas-base <toml>] [--metricas-gerar <toml>]\n";
        return 2;
    }
    fs::path boundaryIn, boundaryOut, metricsBase, metricsOut, probe, generated, runtimeDir;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) {
            std::cerr << "opcao sem valor: " << a << "\n";
            return 2;
        }
        if (a == "--fronteira") boundaryIn = argv[++i];
        else if (a == "--fronteira-gerar") boundaryOut = argv[++i];
        else if (a == "--metricas-base") metricsBase = argv[++i];
        else if (a == "--metricas-gerar") metricsOut = argv[++i];
        else if (a == "--sonda") probe = argv[++i];
        else if (a == "--gerado") generated = argv[++i];
        else if (a == "--runtime") runtimeDir = argv[++i];
        else {
            std::cerr << "opcao desconhecida: " << a << "\n";
            return 2;
        }
    }
    try {
        const auto     r   = run(argv[1], argv[2], probe);
        const fs::path dir = argv[3];
        fs::create_directories(dir);
        writeScan(r->scan, dir);
        writeSdk(r->sdk, dir);
        writePrefSq(r->scan, r->sdk, dir);
        writeBoundary(r->boundary, dir);

        // fronteira usada no fechamento: a declarada (se dada) ou a derivada
        DeclaredBoundary declared;
        if (!boundaryIn.empty()) declared = load_boundary(boundaryIn);
        else declared.entries = r->boundary;
        std::set<uint32_t> bset;
        for (const auto& [addr, e] : declared.entries) bset.insert(addr);
        const ClosureResult closure = check_closure(r->scan, r->sdk, bset);
        writeClosure(closure, dir);
        writeDominance(r->scan, r->sdk, bset, dir);
        Metrics m = compute_metrics(r->scan, r->sdk, declared.entries, closure, declared.providers);
        if (!generated.empty()) m.merge(generatedMetrics(generated));
        if (!runtimeDir.empty()) m.merge(runtimeMetrics(runtimeDir, dir / "runtime_ciclos.tsv"));
        writeText(dir / "metricas.toml", metrics_toml(m));
        if (!boundaryOut.empty()) {
            // entradas com provedor nativo: as fronteiras migradas ([native] slots no config)
            std::map<uint32_t, std::string> providers;
            for (const auto& [addr, e] : r->boundary)
                if (r->cfg.native_slots.count(addr)) providers[addr] = "nativo";
            writeText(boundaryOut, boundary_toml(r->boundary, providers));
        }
        if (!metricsOut.empty()) writeText(metricsOut, metrics_toml(m));

        std::cout << "funcoes: " << m.at("funcoes.total") << " (jogo " << m.at("camada.jogo") << ", sdk "
                  << m.at("camada.sdk") << ", desconhecida " << m.at("camada.desconhecida") << ", conflito "
                  << m.at("camada.conflito") << ")\n"
                  << "hardware: " << m.at("hw.refs.total") << " referencias (" << m.at("hw.refs.acessos")
                  << " acessos, " << m.at("hw.refs.constantes") << " constantes), no jogo: " << m.at("hw.refs.camada.jogo")
                  << "\nfronteira: " << m.at("fronteira.total") << " (" << m.at("fronteira.direta") << " diretas, "
                  << m.at("fronteira.indireta") << " indiretas; legado " << m.at("fronteira.provedor.legado")
                  << ", nativo " << m.at("fronteira.provedor.nativo") << ")\n"
                  << "fechamento: " << closure.reached << " alcancadas, violacoes " << m.at("fechamento.violacoes.sdk")
                  << " (sdk) + " << m.at("fechamento.violacoes.hw") << " (hw); indiretos nao resolvidos "
                  << m.at("fechamento.indireto_nao_resolvido") << "\n";

        int rc = 0;
        if (!boundaryIn.empty() && !closure.violations.empty()) {
            std::cout << "FECHAMENTO VIOLADO: ver fechamento.tsv\n";
            rc = 3;
        }
        if (!metricsBase.empty()) {
            const auto fails = ratchet(parse_metrics(readText(metricsBase)), m);
            for (const auto& f : fails)
                std::cout << "REGRESSAO: " << f.key << " " << f.base << " -> " << f.now << "\n";
            if (!fails.empty() && rc == 0) rc = 4;
        }
        return rc;
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << "\n";
        return 1;
    }
}
