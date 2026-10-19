#pragma once
// Provedor nativo do SDK de perifericos "pd" (N3-2, docs/N3-PLANO.md secao 3; --controles nativo).
//
// Caminho: InputSystem (native/platform/input.h) -> este adaptador -> PDS_PERIPHERAL na RAM guest ->
// jogo. Sem Maple: no modo nativo o barramento fica sem controle (maple_set_controller_present) e
// nada aqui le registrador, tabela de DMA ou resposta Maple. Tres slots (config [native] slots):
//   8C0975C0  disparo do DMA da camada per no VBlank: so o PONTO DE AMOSTRAGEM (latch), o mesmo
//             instante em que o legado tirava a amostra (decisao 2 do plano). O original continua
//             rodando depois: a camada per sonda as portas (VMU ate o N4), sem controle nenhum.
//   8C15C4E6  servidor pd: escreve as 24 unidades pelo contrato do N3-0 (pad_contract): old lido da
//             propria RAM (com as escritas do jogo), on/off/press/release pelo modo do servidor,
//             gatilhos, analogicos, support, name, info e a tabela de estado.
//   8C15C668  pdGetPeripheral(porta): 8C2D8970 + 0x34 * porta, com os mesmos efeitos (pilha, MACL).
// O init 8C15C10C NAO e substituido (inicializa per/Maple, que a VMU usa ate o N4).
//
// Presenca: o SDK so ve um controle depois de identifica-lo (DEVICE_INFO) e o perde quando uma
// leitura nao tem resposta; a amostra de um disparo so chega ao servidor depois da coleta do
// disparo seguinte (8C08F332 roda antes de 8C0975C0 no mesmo VBlank). O adaptador reproduz essa
// semantica sobre as amostras do InputSystem (identificar -> ler; leitura sem controle -> perder),
// sem quadro nem resposta Maple. O indice das amostras (`reads`) e o numero da leitura do legado.
//
// info/name da porta A: o legado aponta sempre para o buffer da camada per (8C2CFFA8), cujo conteudo a
// camada troca na coleta (info do controle identificado / "(none)"); com a porta vazia no Maple ele fica
// "(none)". Aqui o ponteiro e fixo na area nativa do BIOS HLE (kHleNativePadInfo) e o conteudo segue o
// modelo: info do DEVICE_INFO legado, sintetizada, com o controle identificado; senao a copia do buffer
// da camada per. Conferida antes de cada escrita (escrita alheia = erro). As outras 23 unidades apontam
// para os buffers da camada per, como no legado.
//
// Some no N9, quando os consumidores do jogo forem elevados.
#include "boundary_registry.h"
#include "platform/input.h"

#include <cstdint>
#include <string>

namespace bs::transitional::pad {

constexpr uint32_t kServer     = 0x8C15C4E6;
constexpr uint32_t kGetter     = 0x8C15C668;
constexpr uint32_t kDmaTrigger = 0x8C0975C0;
// per_info(i) (8C08F4CC -> 8C0979FA): *(*(8C097AAC)) + (int16)*(8C097AA2) + 120 * i
constexpr uint32_t kPerInfoPtrLiteral = 0x8C097AAC, kPerInfoOffLiteral = 0x8C097AA2, kPerInfoStride = 120;
// per_raw(i) (8C08F4B6): *(8C08F578) + 24 * i; +2 erro, +4 resposta, +8 quadro (condicao em quadro + 8)
constexpr uint32_t kPerRawLiteral = 0x8C08F578, kPerRawStride = 24;
constexpr uint32_t kControllerFunction = 0xFE060F00;  // dados da funcao do controle padrao (DEVICE_INFO)

// Negativos do teste do N3-2 (BS_PAD_NATIVO_MUTACAO=<lista>): cada um tem de ser acusado.
enum NativeMutation : uint32_t {
    kNatNone         = 0,
    kNatOldHost      = 1u << 0,  // old_host: old do on nativo anterior, nao da RAM
    kNatPressOn      = 1u << 1,  // press_on: press = on (sem borda)
    kNatReleaseZero  = 1u << 2,  // release_zero: release sempre 0
    kNatSampleEarly  = 1u << 3,  // amostra_adiantada: o servidor usa a amostra do ultimo disparo, nao a coletada
    kNatInfoNoConnect = 1u << 4, // info_sem_conectado
    kNatPresenceNow  = 1u << 5,  // presenca_imediata: le sem identificar (reconexao um disparo antes)
    kNatMaple        = 1u << 6,  // maple: o controle continua no Maple com a ponte (retorno indevido ao Maple)
};
uint32_t parse_mutations(const std::string& list);  // nomes acima separados por virgula; desconhecido: lanca

struct Stats {
    uint64_t triggers = 0, dmaTriggers = 0, servers = 0, getters = 0;  // dmaTriggers: o original disparou (r0 >= 0)
    uint64_t latches = 0, presence = 0;   // amostras completas / consultas so de presenca
    uint64_t reads = 0;                   // leituras com resposta (o g_condSeq do legado)
    uint64_t identified = 0, lost = 0;    // controle identificado / perdido
    uint64_t guardFaults = 0;
    uint64_t extraServers = 0;            // passadas do servidor alem da primeira entre dois disparos efetivos
};

// Instala os tres provedores nativos (exige os slots; sem eles, std::runtime_error). `sys` e o dono
// das amostras (o chamador mantem vivo ate uninstall).
// collectAfterServer: false no --tempo experimental (onde o legado entrega o fim do DMA; ver o .cpp).
void  install(BoundaryRegistry& registry, native::input::InputSystem& sys, uint32_t mutations = kNatNone,
              bool collectAfterServer = true);
void  uninstall(BoundaryRegistry& registry);
bool  installed();
Stats stats();
uint64_t    reads();    // indice das amostras (coluna "leituras" do BS_PAD_TRACE no modo nativo)
std::string summary();  // linha "[pd nativo] ..." para o log

// Testes unitarios: o mesmo modelo e as mesmas funcoes, sem registro nem slots (RAM de teste).
namespace test {
void begin(native::input::InputSystem& sys, uint32_t mutations = kNatNone, bool collectAfterServer = true);
void dma_fired(Sh4Context* c);  // um disparo efetivo de 8C0975C0 (coleta do anterior + amostra deste)
void server(Sh4Context* c);   // 8C15C4E6
void getter(Sh4Context* c);   // 8C15C668
void end();
} // namespace test

// Diagnostico BS_PAD_FECHAMENTO=<arquivo> (prova dinamica): conta as entradas nas funcoes geradas do
// SDK pd e da camada per (BS_FN_ENTER) e grava, no fim, as contagens com os contadores do Maple.
void closure_install();  // le o ambiente; nada sem a variavel
void closure_finish(bool nativeMode);

} // namespace bs::transitional::pad
