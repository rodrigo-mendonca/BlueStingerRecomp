#pragma once
// Geracao de C++ a partir da analise (ARCHITECTURE.md secao 5).
#include "analysis/analysis.h"

#include <filesystem>
#include <set>

namespace bs::codegen {

struct Options {
    std::filesystem::path out_dir;
    bool                  inline_pool    = true;  // constantes do pool viram literais
    bool                  comments       = true;  // disassembly como comentario
    uint32_t              funcs_per_file = 400;
    // Biblioteca de threads cooperativa ([threads] no TOML): chamadas a setjmp marcam o
    // retorno como ponto de reentrada; o rts das funcoes longjmp troca de contexto.
    std::set<uint32_t>    setjmp_funcs, longjmp_funcs;
    // Funcoes chamadas sempre pelo slot, em todos os builds ([native] slots no TOML): fronteiras
    // migraveis do port nativo, onde o provedor (legado/nativo) e trocado em execucao.
    std::set<uint32_t>    always_slot;
};

struct Result {
    size_t files = 0, functions = 0, lines = 0;
};

Result generate(const analysis::Image& img, const analysis::Analysis& a, const Options& opt);

// Exposto para testes: codigo C++ de uma funcao.
std::string emitFunction(const analysis::Image& img, const analysis::Analysis& a, const analysis::Function& f,
                         const Options& opt);

} // namespace bs::codegen
