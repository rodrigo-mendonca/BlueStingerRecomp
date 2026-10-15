#pragma once
// N0: fronteira entre o jogo e o SDK/hardware, e a verificacao de fechamento.
//
// Fronteira = funcoes do SDK que o jogo pode chamar (a "interface planejada" que o port nativo
// substitui subsistema por subsistema):
//  - direta: funcao do SDK (ou de camada desconhecida) chamada diretamente pelo jogo;
//  - indireta: funcao do SDK cujo endereco o codigo do jogo toma (chamavel por ponteiro), ou
//    alvo de chamada indireta do jogo observado pela sonda (observada).
// A fronteira declarada fica versionada (config/fronteiras_us.toml). O fechamento parte das
// funcoes do jogo, anda pelas chamadas diretas sem atravessar a fronteira e, se alcancar uma
// chamada indireta, considera todos os alvos de indirect_targets(). Violacao:
//  - funcao do SDK fora da fronteira alcancada (chamada que contorna a interface); o
//    fechamento nao segue adiante dela (a violacao e a entrada, nao o que vem atras);
//  - funcao com acesso direto ao hardware alcancada (inclusive do proprio jogo).
#include "hwscan.h"
#include "sdkid.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace bs::hwscan {

enum class BoundaryKind : uint8_t { Direct, Indirect };
const char* boundary_kind_name(BoundaryKind k);

struct BoundaryEntry {
    uint32_t                  address = 0;
    BoundaryKind              kind    = BoundaryKind::Direct;
    Family                    family  = Family::Unknown;
    std::set<dcmap::Subsystem> subsystems;  // hardware alcancavel a partir dela (dentro do SDK)
    int                       callSites = 0;  // chamadas diretas vindas do jogo
    bool                      observed  = false;  // chamada indireta do jogo vista pela sonda
    std::string               name;
};

// Alvos possiveis de uma chamada indireta do jogo: funcoes cujo endereco o codigo do jogo
// toma, e funcoes do jogo com endereco em dado (tabelas de metodos do jogo).
std::set<uint32_t> indirect_targets(const Scan& s, const SdkId& id);
// Funcoes do SDK com endereco em dado (dono do dado desconhecido): fora do fechamento estatico;
// a sonda dinamica registra as chamadas indiretas reais.
std::set<uint32_t> data_only_sdk_targets(const Scan& s, const SdkId& id);

// Subsistemas de hardware alcancaveis por chamadas diretas a partir de cada funcao (ela inclusive).
std::map<uint32_t, std::set<dcmap::Subsystem>> hw_reach(const Scan& s);

std::map<uint32_t, BoundaryEntry> derive_boundary(const Scan& s, const SdkId& id);

// Manifesto com dominancia: para cada funcao que acessa hardware, as entradas da fronteira que
// a alcancam por chamadas diretas/observadas dentro do lado SDK. Com o fechamento valido
// (nenhum caminho do jogo contorna a fronteira), todo caminho do jogo ate ela passa por uma
// dessas entradas. Conjunto vazio = so alcancada pelo runtime (interrupcao, inicio de thread).
std::map<uint32_t, std::set<uint32_t>> hw_dominance(const Scan& s, const SdkId& id, const std::set<uint32_t>& boundary);

struct Violation {
    enum Kind : uint8_t { SdkBypass, HardwareAccess } kind = SdkBypass;
    uint32_t function = 0;  // funcao alcancada
    uint32_t from     = 0;  // quem a alcancou (0 = raiz/indireta)
    bool     viaIndirect = false;
};

struct ClosureResult {
    size_t                 reached = 0;  // funcoes alcancadas a partir do jogo sem atravessar a fronteira
    std::vector<Violation> violations;
    // Funcoes do SDK fora da fronteira com endereco so em dado, possiveis alvos de uma chamada
    // indireta do jogo: o fechamento estatico e inconclusivo para elas (nao conta como prova).
    std::vector<uint32_t>  unresolved;
};

ClosureResult check_closure(const Scan& s, const SdkId& id, const std::set<uint32_t>& boundary);

} // namespace bs::hwscan
