#pragma once
// N6 fase F (docs/N6-PLANO.md secao 18): leitura das musicas do Manatee, portavel. As sequencias (SMSB/SMSD, M*.MSB) e
// os bancos de programas e amostras (SMPB, M*.MPB) do disco do usuario, no formato que o driver do jogo (MANATEE.DRV)
// interpreta; o sequenciador e o sampler (o que o driver faz no tempo) ficam em cima disto.
//
// Entrada do disco do usuario, mas tratada como nao confiavel (mods podem trazer bancos): todo acesso e conferido e um
// arquivo malformado vira erro com motivo, nunca leitura fora dos limites.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bs::native::audio::manatee {

// Um evento da sequencia, como o driver o le (laco das trilhas em 0x4310, despacho em 0x4390 do MANATEE.DRV)
struct SeqEvent {
    enum class Kind : uint8_t { Note, Control, Program, Pressure, Pitch, Reference, Loop, End, Tempo, Ignored } kind;
    uint32_t pos   = 0;  // deslocamento no arquivo
    uint8_t  canal = 0;
    uint8_t  a = 0, b = 0;  // nota: tecla, velocidade; controle: numero, valor; programa/pressao/pitch: valor
    uint32_t gate  = 0;     // nota: duracao em ticks
    uint32_t delta = 0;     // ticks ate o proximo evento
    uint32_t ref   = 0;     // referencia: deslocamento (relativo ao inicio dos eventos); tempo: o valor
    uint8_t  count = 0;     // referencia: quantos eventos; laco: contagem (0 = sem fim)
};

struct Sequence {
    uint32_t              start = 0;  // inicio dos eventos no arquivo (SMSD + 12)
    uint32_t              header1 = 0, header2 = 0;  // +4 e +8 do SMSD (0x88 e o tempo inicial nos 11 do disco)
    std::vector<SeqEvent> events;     // ate o 0x83 inclusive
};

// SMSB: devolve as sequencias ou nullopt com o motivo
std::optional<std::vector<Sequence>> parse_smsb(std::span<const uint8_t> b, std::string* err = nullptr);

// Uma camada de programa (0x30 bytes): os campos do descritor SOSP dos efeitos sem a assinatura, mais as teclas
struct Layer {
    uint32_t sa = 0;     // amostra, relativa ao banco
    uint8_t  flags = 0;  // bit 0 ADPCM da AICA, bit 1 laco (LPCTL)
    uint16_t lsa = 0, lea = 0;
    uint8_t  disdl = 0, tl = 0;
    uint8_t  env[10] = {};  // as 5 palavras do envelope da AICA (bytes 0x16..0x1F), a modelar na fase G
    uint8_t  keyMin = 0, keyMax = 127, keyBase = 60, fine = 0, velMin = 0, velMax = 127;
    uint8_t  raw[0x30] = {};  // a camada inteira (campos ainda sem nome)
};
struct Group {
    uint32_t           flags = 0;
    std::vector<Layer> layers;
};
struct Program {
    std::vector<Group> groups;  // os grupos tocam juntos (camadas)
};
struct Bank {
    std::vector<Program>                 programs;
    std::vector<std::array<uint8_t, 128>> velocityCurves;
    std::span<const uint8_t>             data;  // o arquivo inteiro (as amostras sao relativas a ele)
};

// SMPB: o banco referencia `b` (nao copia); nullopt com o motivo
std::optional<Bank> parse_smpb(std::span<const uint8_t> b, std::string* err = nullptr);

// Tabelas do driver usadas pelo sintetizador, lidas do MANATEE.DRV do disco do usuario (nao copiadas no codigo):
// a curva de volume dos controles 7 e 11 (0x3EC8, 128 bytes). A versao do driver e conferida pela tabela de FNS
// (0x35E8, 1 536 entradas + os 3 limiares de 0x100), que tem de ser floor(1024 * (2^(j/1536) - 1)) nas 1 536.
struct DriverTables {
    std::array<uint8_t, 128> volume{};
};
std::optional<DriverTables> parse_driver_tables(std::span<const uint8_t> drv, std::string* err = nullptr);

} // namespace bs::native::audio::manatee
