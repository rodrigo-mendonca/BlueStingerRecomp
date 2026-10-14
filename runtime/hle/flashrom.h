#pragma once
// Flash do sistema (128 KB) e a syscall FLASHROM do BIOS (HLE).
#include "cpu/sh4_context.h"

#include <filesystem>

namespace bs {

constexpr uint32_t kFlashSize = 128u << 10;

// Conteudo da flash. Sem imagem carregada, a flash e toda 0xFF (apagada) e o jogo ve
// partições sem cabecalho. TODO(M4): gerar a configuracao de sistema padrao para o
// boot nativo (hoje so o bs-diff carrega uma imagem: a mesma do Flycast).
bool     flashrom_load(const std::filesystem::path& path);
uint8_t* flashrom_data();

// r7 = funcao (0 INFO, 1 READ, 2 WRITE, 3 DELETE), argumentos em r4-r6; grava r0.
void flashrom_syscall(Sh4Context* c);

} // namespace bs
