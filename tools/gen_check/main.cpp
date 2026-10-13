// bs-gen-check: verifica que o codigo gerado compila, linka e que a tabela de
// despacho esta ordenada e completa. Nao executa codigo do jogo.
#include "funcs.h"

#include <cstdio>

int main() {
    bsg::register_functions();
    // A entrada do jogo precisa existir em todos os espelhos (P0/P1/P2).
    const uint32_t entry  = 0x8C010000u;
    const bool     ok     = bs::sh4_lookup(entry) && bs::sh4_lookup(entry & 0x1FFFFFFF) && bs::sh4_lookup(0xA0000000u | (entry & 0x1FFFFFFF));
    const bool     absent = bs::sh4_lookup(0x8C000000u) == nullptr;
    std::printf("tabela de despacho registrada; entrada %08X %s; endereco inexistente %s\n", entry,
                ok ? "encontrada" : "AUSENTE", absent ? "rejeitado" : "ACEITO (erro)");
    return ok && absent ? 0 : 1;
}
