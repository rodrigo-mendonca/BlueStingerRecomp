#pragma once
// Boot nativo: le o IP.BIN e o executavel de boot da imagem de disco do usuario e monta o
// estado em que o BIOS entrega o controle ao jogo (RAM, registradores, PVR), sem BIOS nem
// snapshot do Flycast. O codigo recompilado so vale para o executavel com o hash da
// versao suportada; outro executavel e recusado.
#include "cpu/sh4_context.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bs {

struct BootFiles {
    std::vector<uint8_t> ipBin;      // 32 KB
    std::vector<uint8_t> firstRead;  // 1ST_READ.BIN desembaralhado
    std::string          product, version, title, versionId;
};

// Le os arquivos de boot. Lanca std::runtime_error (imagem invalida, executavel ausente
// ou de versao nao suportada).
BootFiles boot_read_disc(const std::filesystem::path& image);

// O mesmo a partir dos dados preparados pelo Builder (runtime_data.h), sem o CDI. Lanca
// disc::RuntimeDataError (dados ausentes/incompletos ou de outra versao).
BootFiles boot_read_data(const std::filesystem::path& dir);

// Estado na entrada do jogo (0xAC010000), como o BIOS o deixa: RAM (area do sistema,
// IP.BIN em 0x8C008000, executavel em 0x8C010000), registradores da CPU e do PVR,
// vetores das syscalls HLE e a hora do RTC (`rtcSeconds`, desde 1950-01-01).
// `ram` aponta para os 16 MB de c.ram. O resto do hardware parte do reset.
void boot_prepare(Sh4Context& c, const BootFiles& files, uint32_t rtcSeconds);

// Segundos desde 1950-01-01 (epoca do RTC do Dreamcast) para a hora local do host.
uint32_t boot_rtc_now();

} // namespace bs
