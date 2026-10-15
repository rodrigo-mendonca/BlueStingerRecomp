#pragma once
// N0: identificacao das funcoes das bibliotecas do SDK Katana (Shinobi, Kamui, Ninja,
// middleware CRI, driver de som `sd`) e do codigo do proprio jogo.
//
// O executavel nao tem simbolos. Cada funcao recebe votos de evidencias independentes:
//  - modulo: a funcao referencia dados do bloco de um modulo (ver Module);
//  - nome: a funcao referencia uma mensagem que a nomeia ("... in mwSndOpenPort",
//    "(ADXT_SetOutPan)", "FT_LoadRoomData(): ...") ou um marcador da biblioteca ("KAMUI",
//    "sd_drv_get_info_.c", "KATANA_FLASH____");
//  - faixa: as bibliotecas sao ligadas em blocos contiguos; uma funcao entre duas sementes
//    (funcoes com nome) da mesma familia, sem semente de outra familia no meio, e da familia;
//  - grafo: todos os chamadores (>= 1) sao da mesma familia (funcao interna da biblioteca);
//  - hardware: acessa diretamente um dispositivo que so aquela familia controla;
//  - assinatura: padrao de codigo conhecido (laco div0s/div1 do runtime de divisao, constante
//    do rand() 0x41C64E6D, angulo de 16 bits do Ninja em `extu.w r4` + `fsca`).
// Confianca: "alta" com duas ou mais evidencias concordantes (ou nome), "media" com uma
// evidencia estrutural, "baixa" so pelo grafo. Conflitos ficam registrados.
#include "hwscan.h"

#include <map>
#include <string>
#include <vector>

namespace bs::hwscan {

// Crt: runtime do compilador e biblioteca C (divisao por software, rand, ...).
enum class Family : uint8_t { Unknown, Game, Shinobi, Kamui, Ninja, Cri, Sound, Bios, Crt };
const char* family_name(Family f);

enum class Confidence : uint8_t { None, Low, Medium, High };
const char* confidence_name(Confidence c);

// Camada: o que importa para a fronteira do port nativo. As bibliotecas nao chamam o jogo
// diretamente (so por ponteiro: callbacks), entao quem chama diretamente uma funcao do jogo e
// do jogo, e tudo que uma biblioteca chama diretamente e do SDK. Sementes: familias com
// confianca media ou alta. Uma funcao alcancada pelas duas regras fica em conflito.
enum class Layer : uint8_t { Unknown, Game, Sdk, Conflict };
const char* layer_name(Layer l);

struct SdkFunction {
    Layer                    layer      = Layer::Unknown;
    Family                   family     = Family::Unknown;
    Confidence               confidence = Confidence::None;
    std::string              name;       // nome extraido de mensagem (dica; vazio se nenhum)
    std::vector<std::string> evidence;   // "nome:mwSndOpenPort", "faixa:cri", "grafo:cri", "hw:render"
    bool                     conflict = false;
};

// Bloco de dados constantes de um modulo. Na imagem, as strings de cada modulo vem antes do
// banner de versao dele ("mwMng:too many libarary" e depois "mwMng Ver.1.10 Build:...";
// "KATANA_FLASH____" e depois "fmMng Ver 1.01 ..."): o bloco vai do fim do banner anterior ao
// fim do proprio banner. So blocos pequenos (<= 2 KB) sao usados como evidencia.
struct Module {
    uint32_t    dataBegin = 0, banner = 0, dataEnd = 0;
    std::string name, text;
    Family      family = Family::Unknown;
    bool        bounded = false;
};

struct SdkId {
    std::map<uint32_t, SdkFunction> functions;  // todas as funcoes da analise
    std::vector<Module>             modules;
    struct Block {
        uint32_t first = 0, last = 0;  // primeira e ultima funcao do bloco de codigo contiguo
        int      switches = 0;         // trocas jogo<->SDK entre funcoes com camada (intercalacao)
    };
    std::vector<Block> blocks;
};

// Banners de versao das bibliotecas na imagem. Exposto para teste.
std::vector<Module> find_modules(const analysis::Image& img);
Family              family_of_module(const std::string& name);

// Familia e nome do "dono" de uma mensagem, ou Unknown. Exposto para teste.
Family owner_of(const std::string& text, std::string* name = nullptr);

SdkId identify(const analysis::Image& img, const analysis::Analysis& a, const Scan& s);

} // namespace bs::hwscan
