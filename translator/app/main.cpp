// bs-recomp: analise (descoberta de funcoes) e geracao de C++.
//   bs-recomp <config.toml> [--game-data DIR] [--out DIR] [--analysis DIR] [--analyze-only]
#include "analysis/analysis.h"
#include "app/config.h"
#include "codegen/codegen.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using namespace bs;

static std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%08X", v);
    return b;
}

static void writeReport(const analysis::Analysis& a, const fs::path& dir) {
    fs::create_directories(dir);
    const auto& s = a.stats;
    std::ofstream r(dir / "report.txt", std::ios::trunc);
    r << "funcoes: " << s.functions << " (chamada " << s.by_call << ", ponteiro no pool " << s.by_pointer
      << ", ponteiro em dados " << s.by_data_pointer << ", config "
      << s.by_config << ")\n"
      << "instrucoes unicas alcancadas: " << s.unique_instructions << "\n"
      << "instrucoes emitidas (com duplicacao entre funcoes): " << s.instructions << "\n"
      << "tabelas de salto: " << s.jump_tables << " (heuristicas: " << s.jump_tables_heuristic << ")\n"
      << "desvios indiretos resolvidos: " << s.direct_indirects << ", via despacho: " << s.unresolved_indirects << "\n"
      << "fins mortos (dado/invalido alcancado): " << s.dead_ends << "\n"
      << "palavras de dado no codigo: " << s.pool_words << "\n"
      << "instrucoes orfas (deve ser 0): " << s.orphans << "\n\navisos:\n";
    for (const auto& w : a.warnings) r << "  " << w << "\n";

    std::ofstream f(dir / "functions.tsv", std::ios::trunc);
    f << "entrada\torigem\tinstrucoes\tlabels\ttail_calls\tdiretos\tdespacho\tfins_mortos\tprimeiro_fim_morto\n";
    static const char* kOrigin[] = {"entry", "call", "pointer", "config", "data_pointer"};
    for (const auto& [addr, fn] : a.functions)
        f << hex(addr) << '\t' << kOrigin[int(fn.origin)] << '\t' << fn.insns.size() + fn.slots.size() << '\t'
          << fn.labels.size() << '\t' << fn.tail_calls.size() << '\t' << fn.direct.size() << '\t'
          << fn.unresolved.size() << '\t' << fn.dead_ends.size() << '\t'
          << (fn.dead_ends.empty() ? std::string("-") : hex(*fn.dead_ends.begin())) << '\n';

    // Mapa de cobertura: 1 byte por halfword da imagem (0 = desconhecido, 1 = codigo, 2 = dado).
    if (!a.code.empty()) {
        const uint32_t    base = *a.code.begin() & ~0xFFFFu;
        std::vector<char> map;
        for (uint32_t x : a.code) { if (map.size() <= (x - base) / 2) map.resize((x - base) / 2 + 1); map[(x - base) / 2] = 1; }
        for (uint32_t x : a.data) if (x >= base) { if (map.size() <= (x - base) / 2) map.resize((x - base) / 2 + 1); if (!map[(x - base) / 2]) map[(x - base) / 2] = 2; }
        std::ofstream cov(dir / "coverage.bin", std::ios::binary | std::ios::trunc);
        cov.write(map.data(), std::streamsize(map.size()));
    }

    std::ofstream j(dir / "jump_tables.tsv", std::ios::trunc);
    j << "insn\ttabela\tentrada\tlimitada\tcasos\n";
    for (const auto& [pc, jt] : a.jump_tables)
        j << hex(pc) << '\t' << hex(jt.table) << '\t' << jt.entry_size << '\t' << (jt.bounded ? "sim" : "nao") << '\t'
          << jt.targets.size() << '\n';
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: bs-recomp <config.toml> [--game-data DIR] [--out DIR] [--analysis DIR] [--analyze-only]\n");
        return 2;
    }
    fs::path gameData = "game-data", out = "generated", analysisDir = "analysis";
    bool     analyzeOnly = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--game-data" && i + 1 < argc) gameData = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--analysis" && i + 1 < argc) analysisDir = argv[++i];
        else if (a == "--analyze-only") analyzeOnly = true;
        else { std::fprintf(stderr, "argumento desconhecido: %s\n", a.c_str()); return 2; }
    }
    try {
        const GameConfig cfg = loadConfig(argv[1]);
        analysis::Image  img;
        img.base = cfg.base;
        {
            std::ifstream f(gameData / cfg.executable, std::ios::binary);
            if (!f) throw std::runtime_error("nao foi possivel abrir " + (gameData / cfg.executable).string() +
                                             " (rode bs-extract antes)");
            img.bytes.assign(std::istreambuf_iterator<char>(f), {});
        }
        const analysis::Analysis a = analysis::analyze(img, cfg.analysis);
        writeReport(a, analysisDir);
        const auto& s = a.stats;
        std::printf("funcoes: %zu | instrucoes: %zu unicas, %zu emitidas | tabelas de salto: %zu | despacho: %zu | "
                    "fins mortos: %zu | orfas: %zu | avisos: %zu\n",
                    s.functions, s.unique_instructions, s.instructions, s.jump_tables, s.unresolved_indirects,
                    s.dead_ends, s.orphans, a.warnings.size());
        if (analyzeOnly) return 0;
        codegen::Options opt;
        opt.out_dir        = out;
        opt.inline_pool    = cfg.inline_pool_constants;
        opt.funcs_per_file = cfg.funcs_per_file;
        const auto r       = codegen::generate(img, a, opt);
        std::printf("gerado: %zu arquivos, %zu funcoes, %zu linhas em %s\n", r.files, r.functions, r.lines,
                    out.string().c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
