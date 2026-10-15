// bs-extract: le a imagem de disco do usuario e extrai IP.BIN, 1ST_READ.BIN
// e os arquivos de dados para game-data/, com manifest.json de hashes.
//   --runtime: prepara os dados de execucao do jogo nativo (runtime_data.h) e confere
//              todos os setores contra a imagem.
#include "disc/cdi_image.h"
#include "extract.h"
#include "runtime_data.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>

static void usage() {
    std::fprintf(stderr,
                 "uso: bs-extract [--info] [--force] <imagem.cdi> [saida]\n"
                 "     bs-extract --runtime <imagem.cdi> <pasta de dados>\n"
                 "  saida      pasta de destino (padrao: game-data)\n"
                 "  --info     so mostra faixas, IP.BIN e hash do executavel\n"
                 "  --force    extrai mesmo com hash de executavel desconhecido\n"
                 "  --runtime  dados de execucao do jogo nativo (o jogo nao le o CDI)\n");
}

static int runRuntime(const std::string& image, const std::string& out) {
    using clock = std::chrono::steady_clock;
    const auto           t0 = clock::now();
    bs::disc::CdiImage   img(image);
    const auto           r  = bs::disc::prepare_runtime_data(img, out);
    const auto           t1 = clock::now();
    std::cout << "dados de execucao: " << r.files << " arquivos, " << r.bytes / (1024 * 1024) << " MB, "
              << r.metaSectors << " setores de sistema (" << r.versionId << ") em "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms\n";
    const std::string diff = bs::disc::verify_runtime_data(img, out);
    const auto        t2   = clock::now();
    std::cout << "conferencia setor a setor: " << (diff.empty() ? "identico a imagem" : diff) << " ("
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << " ms)\n";
    return diff.empty() ? 0 : 1;
}

int main(int argc, char** argv) {
    bs::ExtractOptions opt;
    opt.out = "game-data";
    int positional = 0;
    if (argc == 4 && std::string(argv[1]) == "--runtime") {
        try {
            return runRuntime(argv[2], argv[3]);
        } catch (const std::exception& e) {
            std::cerr << "erro: " << e.what() << "\n";
            return 1;
        }
    }
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
