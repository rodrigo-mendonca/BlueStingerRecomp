#pragma once
// N1: adaptador da API gdFs (Shinobi 1.05) sobre native::files::IFileSystem.
//
// Contratos identificados no executavel (docs/N1-PLANO.md, secao 2). O GDFS do guest vira um
// handle de 32 bits fora da RAM (0x7E______): ponteiros do host nunca vao para o guest, e um
// acesso direto do jogo ao "ponteiro" cai no caminho lento (visivel no log/sonda). Os setores
// de 2 048 bytes sao a unidade da API gdFs (posicao, tamanho, pedidos), convertida aqui em
// deslocamentos de bytes no arquivo; nada de FAD, LBA ou diretorio do disco.
//
// Um GDFS legado (ponteiro na RAM) que chegue a uma funcao migrada vai para a funcao original.
// Um handle nativo que chegue a uma funcao sem equivalente nativo (FAD, PIO, espera interna) e
// erro fatal com mensagem: nunca leitura silenciosa de memoria.
#include "boundary_registry.h"
#include "platform/data_files.h"
#include "platform/files.h"

#include <cstdint>
#include <string>

namespace bs::transitional::gdfs {

// Enderecos das funcoes gdFs no executavel (config/bluestinger_us.toml, [native] slots).
struct Addresses {
    uint32_t open, changeDir, close, getFileSize, getFileSctSize, getFileFad, seek, tell, eof, setTransMode,
        read, reqPio, reqRd32, wait, getStat, getTransStat, stop, getErr, cbReadEnd, cb2, cbErr;
    uint32_t server;  // servidor do gdFs: onde os pedidos assincronos terminam
};

// Blue Stinger NTSC-U V1.000.
constexpr Addresses kBlueStingerUS = {
    0x8C15BF60, 0x8C15B626, 0x8C15B644, 0x8C15B680, 0x8C15B69A, 0x8C15B6B4, 0x8C15B6CE,
    0x8C15B71C, 0x8C15B732, 0x8C15B75A, 0x8C15B774, 0x8C15B7FA, 0x8C15B8D4, 0x8C15B548,
    0x8C15BBF8, 0x8C15BC10, 0x8C15BC26, 0x8C15BD40, 0x8C15BD58, 0x8C15BDA4, 0x8C15BDDE,
    0x8C15AE28,
};

constexpr uint32_t kTokenTag = 0x7E000000;
inline bool        is_native_handle(uint32_t v) { return (v & 0xFF000000u) == kTokenTag && v != kTokenTag; }

// Registra as funcoes no `registry` (subsistema arquivos) e seleciona o provedor nativo.
// Lanca std::runtime_error se alguma funcao nao tem slot (codigo gerado sem [native] slots):
// sem slot, chamadas diretas iriam ao legado com handles nativos.
void install(BoundaryRegistry& registry, native::files::IFileSystem& fs, const Addresses& a = kBlueStingerUS);
// Volta ao legado e fecha os arquivos abertos.
void uninstall(BoundaryRegistry& registry);

// O contrato gdFs guarda o tamanho em 32 bits (+12 bytes, +16 setores no GDFS do binario): um
// recurso ou mod maior nao e representavel. Lanca std::runtime_error com o caminho.
void check_sizes(const native::files::DataFileSystem& fs);

struct Stats {
    uint64_t opens = 0, reads = 0, bytes = 0, notFound = 0, legacyPassThrough = 0;
};
Stats stats();

// Quantas vezes o jogo comecou a ler um arquivo de sala (R*.BIN, leitura do inicio do arquivo): um sinal de troca de
// cenario para quem precisa dele fora do jogo (a camera livre, 2026-10-23). So o provedor nativo conta.
uint64_t room_loads();

} // namespace bs::transitional::gdfs
