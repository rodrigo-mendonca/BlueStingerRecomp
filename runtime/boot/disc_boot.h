#pragma once
// Boot a partir da imagem de disco do usuario (CDI), fora do produto: so ferramentas e
// validacao (Builder, bs-diff, testes com a imagem real) ligam bs_runtime_cdi. O executavel
// do jogo le os dados preparados (boot_read_data, native_boot.h); o mapa do linker dele e
// conferido pelo teste n0_produto_sem_cdi.
#include "boot/native_boot.h"

#include <filesystem>

namespace bs {

// Le o IP.BIN e o executavel de boot do CDI. Lanca std::runtime_error (imagem invalida,
// executavel ausente ou de versao nao suportada).
BootFiles boot_read_disc(const std::filesystem::path& image);

} // namespace bs
