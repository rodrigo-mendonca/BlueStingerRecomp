// bs-extract: le a imagem de disco do usuario e extrai IP.BIN, 1ST_READ.BIN
// e os arquivos de dados para game-data/, com manifest.json de hashes.
#include "extract.h"

#include <cstdio>
#include <exception>
#include <iostream>
#include <string>

static void usage() {
    std::fprintf(stderr,
                 "uso: bs-extract [--info] [--force] <imagem.cdi> [saida]\n"
                 "  saida    pasta de destino (padrao: game-data)\n"
                 "  --info   so mostra faixas, IP.BIN e hash do executavel\n"
                 "  --force  extrai mesmo com hash de executavel desconhecido\n");
}

int main(int argc, char** argv) {
    bs::ExtractOptions opt;
    opt.out = "game-data";
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--info") opt.info_only = true;
        else if (a == "--force") opt.force = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-') { usage(); return 2; }
        else if (positional == 0) { opt.image = a; ++positional; }
        else if (positional == 1) { opt.out = a; ++positional; }
        else { usage(); return 2; }
    }
    if (positional == 0) { usage(); return 2; }
    try {
        return bs::runExtract(opt, std::cout);
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << "\n";
        return 1;
    }
}
