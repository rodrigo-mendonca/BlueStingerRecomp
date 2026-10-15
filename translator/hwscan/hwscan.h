#pragma once
// N0 (docs/PLANO-PORT-NATIVO.md): varredura estatica do executavel do jogo atras de
// dependencias do hardware do Dreamcast, a partir da analise do tradutor.
//
// Por funcao, uma propagacao de constantes linear (por bloco: rotulos zeram o que se sabe;
// chamadas zeram r0-r7, como na convencao do SH-4) resolve:
//  - acessos a memoria com endereco conhecido (base + deslocamento, @(r0,Rn), pos/pre),
//    `pref` (store queues), `movca`/`ocb*`;
//  - literais do pool que sao enderecos de hardware (constantes passadas adiante);
//  - literais que apontam para strings da imagem (identificacao das bibliotecas);
//  - o grafo de chamadas (diretas, bsr, tail calls) e os desvios indiretos.
// Endereco calculado em tempo de execucao nao e resolvido aqui: a sonda dinamica do runtime
// legado (runtime/debug/hw_probe) cobre esses casos.
#include "analysis/analysis.h"
#include "dc_regions.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace bs::hwscan {

enum class Kind : uint8_t { Read, Write, Pref, Cache, Constant };  // Pref: store queue (pref)
const char* kind_name(Kind k);

struct HwRef {
    uint32_t       function = 0, pc = 0, address = 0;  // function 0: palavra de dado (pc = endereco dela)
    uint8_t        size     = 0;
    Kind           kind     = Kind::Read;
    dcmap::Region  region   = dcmap::Region::None;
    uint64_t       count    = 0;      // sonda: vezes observado (0 = so estatico)
    bool           dynamic  = false;  // veio da sonda (pc = 0)
};

// Dynamic: desvio indireto observado pela sonda (alvo conhecido).
enum class EdgeKind : uint8_t { Call, Tail, Indirect, Dynamic };

struct Edge {
    uint32_t caller = 0, callee = 0;  // callee 0 = indireto (desconhecido)
    uint32_t pc     = 0;
    EdgeKind kind   = EdgeKind::Call;
};

struct StringRef {
    uint32_t    pc = 0, address = 0;
    std::string text;
};

struct Scan {
    std::vector<HwRef>                              refs;
    std::vector<Edge>                               edges;
    std::map<uint32_t, std::vector<StringRef>>      strings;       // funcao -> strings referenciadas
    std::set<uint32_t>                              addressTaken;  // funcoes com endereco em dado/pool
    std::map<uint32_t, std::set<uint32_t>>          takenBy;       // funcao -> quem toma o endereco (0 = dado)
    std::set<uint32_t>                              literals;      // palavras do pool lidas por mov.l @(d,PC)
    std::map<uint32_t, std::set<uint32_t>>          dataRefs;      // funcao -> enderecos da imagem (dado) referenciados
    // `pref @Rn` com Rn desconhecido (p.ex. lido de uma variavel global): store queue (envio ao
    // TA) ou so prefetch de cache; a sonda separa. E o literal 0xE0000000 (base das SQ, mas
    // tambem mascara do segmento), por funcao: candidatas a uso de store queue.
    std::map<uint32_t, int>                         prefUnresolved;
    std::map<uint32_t, int>                         sqLiteral;
};

Scan scan(const analysis::Image& img, const analysis::Analysis& a);

// Literal do pool com forma de endereco de hardware (filtra mascaras, floats e padroes).
bool plausible_constant(uint32_t a);

// String ASCII imprimivel (>= 4 caracteres, terminada em 0) no endereco, ou vazia.
std::string string_at(const analysis::Image& img, uint32_t addr);

} // namespace bs::hwscan
