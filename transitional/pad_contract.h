#pragma once
// Contrato do servidor de perifericos do SDK (Shinobi "pd 1.07a"), N3-0 (docs/N3-PLANO.md).
//
// O jogo nao fala com o Maple: le uma tabela de 24 PDS_PERIPHERAL (4 portas x 6 unidades, 0x34
// bytes cada, em 8C2D8970) que o servidor 8C15C4E6 reescreve a cada passo de jogo, e o ponteiro
// que pdGetPeripheral (8C15C668) devolve. Este arquivo descreve, sem Sh4Context e sem nada do
// protocolo, o que o servidor escreve em cada unidade a partir do que a camada "per" entrega
// (8C08F4B6: condicao bruta; 8C08F4CC: info do dispositivo). E a referencia do provedor nativo
// (N3-2) e do teste de contrato contra o legado (pad_probe).
//
// Ordem do servidor por unidade i (disassembly de 8C15C4E6..8C15C656):
//   info = per_info(i); old = on (da propria RAM, ja com as escritas do jogo); status[i] = f(resposta)
//   i % 6 != 0 (subunidade): name = info + 20; nada mais
//   i % 6 == 0: controle (flags & 0x21, erro 0, resposta 8) -> id/support/name (8C15C3D4) e decode
//               (8C15C24C); teclado (flags & 0x40, idem) -> 8C17B60C (fora do contrato); senao
//               "sem dispositivo" (8C15C1A2); depois off = ~on e as bordas press/release pelo modo
#include <cstdint>

namespace bs::transitional::pad {

constexpr uint32_t kTable        = 0x8C2D8970;  // PDS_PERIPHERAL[24]
constexpr uint32_t kUnitSize     = 0x34;
constexpr int      kUnits        = 24;
constexpr uint32_t kStatusTable  = 0x8C2D8E50;  // uint32_t[24], codigo de estado por unidade
constexpr uint32_t kModeAddr     = 0x8C2D8EB0;  // modo do servidor (0 no jogo)
constexpr uint32_t kNoDeviceName = 0x8C1DACAE;  // "(no device)"
constexpr uint32_t kGamePdsPtr   = 0x8C24CCB0;  // PDS* escolhido pelo jogo (8C064878)

struct Pds {
    uint32_t id = 0, support = 0, on = 0, off = 0, press = 0, release = 0;
    uint16_t r = 0, l = 0;
    int16_t  x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    uint32_t name = 0, extend = 0, old = 0, info = 0;
    bool     operator==(const Pds&) const = default;
};
Pds  pds_load(const uint8_t* p);  // 0x34 bytes, little-endian
void pds_store(uint8_t* p, const Pds& s);

// O que a camada per entrega ao servidor para uma unidade.
struct Unit {
    uint32_t info      = 0;  // endereco guest da info (8C08F4CC)
    uint32_t infoFlags = 0;  // *(info + 0): bit 0 controle, 0x20 outro tipo com dados, 0x40 teclado
    uint32_t support   = 0;  // dados da funcao como o SDK os le (8C15C3D4): os bytes de info+4 invertidos
    uint8_t  error     = 0;  // byte +2 da descricao bruta
    uint8_t  response  = 0;  // byte +4: codigo da resposta Maple (8 = dados)
    uint8_t  cond[8]   = {}; // condicao: 0-1 botoes (ativos em 0), 2 gatilho R, 3 gatilho L, 4-7 eixos x1 y1 x2 y2
};

enum class Kind { Subunit, Controller, Keyboard, Absent };
Kind classify(int unit, const Unit& u);

// Codigo de estado (8C15C490) pelo byte de resposta.
uint32_t status_code(uint8_t response);

// Decodificador (8C15C24C): on (antes das bordas), r, l, x1, y1, x2, y2 pelo `support` ja escrito.
// O gatilho conta como botao digital (on 0x10000 para R, 0x20000 para L) com o bit 7 do byte (>= 128).
void decode_controller(Pds& p, const uint8_t cond[8], uint32_t mode);

// Bordas (8C15C5C6..62A): off = ~on; modo 0: press = (on^old) & on, release = (on^old) & off;
// modo != 0: press = ~(on^old) | on, release = ~(on^old) | off.
void edges(Pds& p, uint32_t mode);

// "Sem dispositivo" (8C15C1A2) antes das bordas.
void clear_absent(Pds& p, uint32_t mode);

// Mutacoes do teste de contrato (negativos C4): um contrato errado de proposito tem de divergir
// do legado. old_nativo usa `nativeOld` (o on decodificado no servidor anterior) em vez da RAM.
enum Mutation : uint32_t {
    kMutNone          = 0,
    kMutSwapAB        = 1u << 0,  // A e B trocados em on
    kMutTrigger127    = 1u << 1,  // gatilho digital a partir de 127
    kMutOldNative     = 1u << 2,  // old vindo do estado nativo, nao da RAM
    kMutInfoNoConnect = 1u << 3,  // info sintetizada sem o bit de conectado
};

// Uma passada do servidor numa unidade: `before` e a unidade na RAM antes do servidor. Devolve a
// unidade depois do servidor. Teclado: devolve `before` com info/old/status ja atualizados, e o
// chamador trata como fora do contrato.
Pds server_unit(int unit, const Pds& before, const Unit& u, uint32_t mode, uint32_t mutations = kMutNone,
                uint32_t nativeOld = 0);

// Info do controle padrao como o legado a monta do DEVICE_INFO (conteudo que o provedor nativo
// sintetiza no N3-2; medido na RAM, analysis/n3/dumps): +0 flags (1 = controle), +4 funcao (bytes
// como na RAM), +0x10 01 e area FF, +0x14 nome (30 bytes com espacos, terminador), +0x34 licenca (60
// bytes com espacos, terminador), +0x74 consumo em espera 0x01AE e maximo 0x01F4 (decimos de mA).
constexpr int kInfoBytes = 0x78;
void controller_info(uint8_t out[kInfoBytes], uint32_t functionData, uint32_t mutations = kMutNone);

} // namespace bs::transitional::pad
