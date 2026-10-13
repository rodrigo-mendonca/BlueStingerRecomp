// bs-diff: testes diferenciais do codigo recompilado contra estados capturados
// no Flycast (tools/dev/flycast_oracle.py).
//
//   bs-diff call  <func> <entrada.bss> <saida.bss>   executa a funcao recompilada
//                                                    a partir de <entrada> e compara
//                                                    com o estado do Flycast em <saida>
//   bs-diff trace <func> <entrada.bss> <trace.tsv> [max]
//                                                    grava cada entrada de funcao
//                                                    (endereco + registradores), ate
//                                                    `max` chamadas (padrao 100000)
//
// Codigo de saida: 0 = igual, 1 = divergencia ou erro fatal, 2 = uso.
#include "debug/snapshot.h"
#include "funcs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bs;

namespace {

struct FatalError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Fim pedido do trace (`max` chamadas): nao e erro.
struct TraceLimit {};

void onFatal(Sh4Context*, const char* what, uint32_t pc, uint32_t extra) {
    char b[256];
    std::snprintf(b, sizeof(b), "%s em PC=%08X (valor %08X)", what, pc, extra);
    throw FatalError(b);
}

std::FILE* g_traceOut   = nullptr;
size_t     g_traceCount = 0;
size_t     g_traceMax   = 100000;

void onEnter(Sh4Context* c, uint32_t entry) {
    if (g_traceOut && g_traceCount == g_traceMax)
        throw TraceLimit{};
    ++g_traceCount;
    if (!g_traceOut) return;
    std::fprintf(g_traceOut, "%08X", entry);
    for (int i = 0; i < 16; ++i) std::fprintf(g_traceOut, "\t%08X", c->r[i]);
    std::fprintf(g_traceOut, "\t%08X\t%08X\t%08X\t%08X\n", c->pr, sh4_get_sr(*c), c->fpscr, c->fpul);
}

int run(bool trace, uint32_t func, const char* entryPath, const char* outPath) {
    bsg::register_functions();
    sh4_set_fatal_handler(onFatal);
    sh4_set_trace(onEnter);

    const Snapshot       in = Snapshot::load(entryPath);
    std::vector<uint8_t> ram(16u << 20);
    Sh4Context           c{};
    c.ram = ram.data();
    in.apply(c);
    c.ret_expect = c.pr;
    c.next_event = ~0ull;  // sem eventos durante o teste
    // O snapshot nao tem ciclos nem registradores de hardware (CCR, porta A, PVR,
    // Holly): tudo comeca do reset. Divergencia em funcao que leia hardware antes de
    // escreve-lo pode vir disso, e nao do codigo recompilado.
    std::fprintf(stderr, "aviso: estado de hardware e ciclos partem do reset (fora do snapshot)\n");

    Sh4Func f = sh4_lookup(func);
    if (!f) {
        std::fprintf(stderr, "funcao %08X nao existe no codigo gerado\n", func);
        return 1;
    }
    if (trace) {
        g_traceOut = std::fopen(outPath, "w");
        if (!g_traceOut) { std::fprintf(stderr, "nao foi possivel criar %s\n", outPath); return 1; }
    }
    std::string fatal;
    try {
        f(&c);
    } catch (const FatalError& e) {
        fatal = e.what();
    } catch (const TraceLimit&) {
        std::printf("limite de %zu chamadas atingido\n", g_traceMax);
    }
    if (g_traceOut) std::fclose(g_traceOut);
    std::printf("funcoes executadas: %zu\n", g_traceCount);
    if (!fatal.empty()) {
        std::printf("ERRO FATAL: %s\n", fatal.c_str());
        return 1;
    }
    if (trace) return 0;

    const Snapshot expected = Snapshot::load(outPath);
    const Snapshot actual   = Snapshot::capture(c, expected.regs.pc);
    const auto     d        = compare(expected, actual);
    if (d.empty()) {
        std::printf("IGUAL: registradores, FPU e 16 MB de RAM conferem com o Flycast\n");
        return 0;
    }
    std::printf("DIVERGENCIAS: %zu\n", d.size());
    for (size_t k = 0; k < d.size() && k < 40; ++k)
        std::printf("  %-16s flycast=%08X recompilado=%08X\n", d[k].what.c_str(), d[k].expected, d[k].actual);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    const bool trace = argc >= 2 && std::strcmp(argv[1], "trace") == 0;
    if (!(argc == 5 || (trace && argc == 6)) || (!trace && std::strcmp(argv[1], "call") != 0)) {
        std::fprintf(stderr, "uso: bs-diff call <func> <entrada.bss> <saida.bss>\n"
                             "     bs-diff trace <func> <entrada.bss> <trace.tsv> [max]\n");
        return 2;
    }
    if (argc == 6) g_traceMax = std::strtoull(argv[5], nullptr, 0);
    try {
        return run(trace, uint32_t(std::strtoul(argv[2], nullptr, 0)), argv[3], argv[4]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
