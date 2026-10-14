#pragma once
// Fonte de setores lida da imagem de disco do usuario (CDI hoje; GDI pelo mesmo
// DiscImage). Usada pelo bs-diff e pelo runtime enquanto a origem definitiva dos
// dados do disco nao for decidida (ver STATUS.md, "Em aberto").
#include "gdrom/gdrom.h"

#include <filesystem>

namespace bs {

std::unique_ptr<DiscSource> open_disc_image(const std::filesystem::path& path);

} // namespace bs
