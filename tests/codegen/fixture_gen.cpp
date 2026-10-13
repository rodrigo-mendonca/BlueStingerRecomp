// Gera C++ para o programa sintetico em tempo de build (usado por test_exec).
#include "../fixtures/sh4_fixture.h"
#include "analysis/analysis.h"
#include "codegen/codegen.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    bs::analysis::Image img;
    img.base = bs::fixture::kBase;
    for (uint16_t w : bs::fixture::kWords) {
        img.bytes.push_back(uint8_t(w));
        img.bytes.push_back(uint8_t(w >> 8));
    }
    bs::analysis::Config cfg;
    cfg.entry    = bs::fixture::kMain;
    const auto a = bs::analysis::analyze(img, cfg);
    bs::codegen::Options opt;
    opt.out_dir        = argv[1];
    opt.funcs_per_file = 1000;
    opt.inline_pool    = argc > 2 && argv[2][0] == '1';
    const auto r       = bs::codegen::generate(img, a, opt);
    std::printf("fixture: %zu funcoes\n", r.functions);
    return 0;
}
