// Analise do executavel real (so com BS_DISC_IMAGE; senao 77 = SKIP). Regressao do NEW GAME:
// PC 8C1953E2 desviava pelo despacho para 8C194580 ("rts; mov #0,r0"), metodo da vtable em
// 8C1DBF58 que o analisador nao conhecia. Todas as entradas dessa tabela precisam ser
// funcoes, e a analise continua sem fins mortos nem instrucoes orfas.
#include "../unit/test.h"

#include "analysis/analysis.h"
#include "app/config.h"
#include "boot/native_boot.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

using namespace bs;

static int run() {
    const char* path = std::getenv("BS_DISC_IMAGE");
    if (!path || !*path) {
        std::printf("BS_DISC_IMAGE nao definido: teste ignorado\n");
        return 77;
    }
    const BootFiles  boot = boot_read_disc(path);
    const GameConfig cfg  = loadConfig("config/bluestinger_us.toml");
    analysis::Image  img;
    img.base  = cfg.base;
    img.bytes = boot.firstRead;
    for (const auto& r : cfg.relocated) img.relocated.push_back(r);
    const analysis::Analysis a = analysis::analyze(img, cfg.analysis);

    // a vtable inteira: 8C1DBF58..8C1DBF88 (11 metodos, varios "return k" de 2 instrucoes)
    for (uint32_t at = 0x8C1DBF58; at <= 0x8C1DBF88; at += 4) {
        uint32_t target;
        std::memcpy(&target, boot.firstRead.data() + (at - cfg.base), 4);
        if (!a.functions.count(target)) std::printf("  falta funcao %08X (tabela em %08X)\n", target, at);
        BS_CHECK(a.functions.count(target));
    }
    BS_CHECK(a.functions.count(0x8C194580) && a.functions.at(0x8C194580).origin == analysis::Origin::PointerTable);
    BS_CHECK(a.stats.dead_ends == 0 && a.stats.orphans == 0);
    BS_CHECK(a.stats.functions >= 4083);
    std::printf("funcoes %zu (tabela de ponteiros %zu), fins mortos %zu, orfas %zu\n", a.stats.functions,
                a.stats.by_pointer_table, a.stats.dead_ends, a.stats.orphans);
    return bs::test::failures() ? 1 : 0;
}

int main() {
    try {
        return run();
    } catch (const std::exception& e) {
        std::printf("excecao: %s\n", e.what());
        return 1;
    }
}
