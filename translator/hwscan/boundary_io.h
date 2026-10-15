#pragma once
// N0: fronteira declarada em TOML (config/fronteiras_us.toml).
//
//   [[fronteira]]
//   endereco    = 0x8C08BD00
//   tipo        = "direta"            # ou "indireta"
//   familia     = "shinobi"
//   subsistemas = ["sistema"]
//   provedor    = "legado"            # "nativo" quando um subsistema N1-N9 assumir
//   nome        = ""
#include "closure.h"

#include <filesystem>
#include <map>
#include <string>

namespace bs::hwscan {

struct DeclaredBoundary {
    std::map<uint32_t, BoundaryEntry> entries;
    std::map<uint32_t, std::string>   providers;  // endereco -> "legado" | "nativo"
};

std::string      boundary_toml(const std::map<uint32_t, BoundaryEntry>& b,
                               const std::map<uint32_t, std::string>& providers = {});
DeclaredBoundary parse_boundary(const std::string& text);  // lanca std::runtime_error
DeclaredBoundary load_boundary(const std::filesystem::path& p);

} // namespace bs::hwscan
