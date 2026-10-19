#pragma once
// Provedor nativo dos saves (N4, --saves nativo): a API publica da biblioteca de backup "bu" do
// Katana passa a ser atendida por bu::Machine sobre um ISaveStore (native/platform/saves.h), sem VMU,
// Maple nem a FAT do cartao. Inventario e oraculo em docs/N4-PREPARACAO.md.
//
// Slots ([native] slots): as 13 entradas publicas e o servidor periodico.
//   8C08BDEC init: a maquina habilita as unidades da mascara; o servidor 8C08BDE8 e registrado no
//            agendador do jogo (8C17BFB4, prioridade 0x0A00) como no original, e o callback do 4o
//            argumento (que registra os dois callbacks do jogo) e chamado.
//   8C08BDE8 servidor: uma passada da maquina; entrega progresso e conclusoes aos callbacks.
//   8C08C074 / 8C08C07A  registro dos callbacks de conclusao / progresso.
//   8C08CA34 mount, 8C08C1FC disk_info, 8C08C636 exists, 8C08C8D4 load, 8C08C818 save,
//   8C08C696 status, 8C08C112 ultimo erro, 8C08BEBC exit.
//   8C08C7BE (op 0x0B, so alcancavel por literal sem chamador): registra e devolve -1.
// As rotinas puras do VMS (8C08CC58 tamanho, 8C08CA5C montagem com CRC) continuam recompiladas.
//
// Some no N9, quando os chamadores do jogo forem elevados.
#include "boundary_registry.h"
#include "platform/saves.h"

#include <cstdint>
#include <string>

namespace bs::transitional::bu {

constexpr uint32_t kInit = 0x8C08BDEC, kServer = 0x8C08BDE8, kRegDone = 0x8C08C074, kRegProgress = 0x8C08C07A,
                   kMount = 0x8C08CA34, kDiskInfo = 0x8C08C1FC, kExists = 0x8C08C636, kLoad = 0x8C08C8D4,
                   kSave = 0x8C08C818, kStatus = 0x8C08C696, kLastError = 0x8C08C112, kExit = 0x8C08BEBC,
                   kUnknown0B = 0x8C08C7BE;
// Registro do servidor no agendador (os mesmos argumentos do init original, 8C08BE3C..8C08BE50)
constexpr uint32_t kSchedRegister = 0x8C17BFB4, kSchedPriority = 0x0A00, kSchedArgPtr = 0x8C0B92EC,
                   kSchedHandle = 0x8C2B2D94, kInitFlag = 0x8C157D7C;

struct Stats {
    uint64_t inits = 0, ticks = 0, mounts = 0, exists = 0, saves = 0, loads = 0, statusBusy = 0;
    uint64_t saveOk = 0, saveFail = 0, loadOk = 0, loadFail = 0, unsupported = 0;
};

// Instala o provedor (exige os slots; sem eles, std::runtime_error). `store` vive ate uninstall.
void        install(BoundaryRegistry& registry, native::saves::ISaveStore& store);
void        uninstall(BoundaryRegistry& registry);
bool        installed();
Stats       stats();
std::string summary();  // linha "[saves] ..." para o log
void        h1_install();  // diagnostico BS_SAVES_H1=1 (build com mods; ver o .cpp)

} // namespace bs::transitional::bu
