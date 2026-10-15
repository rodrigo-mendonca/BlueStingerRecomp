#pragma once
// Registro das fronteiras migraveis e escolha de provedor (terceira camada das fronteiras,
// docs/PLANO-PORT-NATIVO.md secao 2): para cada funcao do SDK na fronteira declarada
// (config/fronteiras_us.toml), o provedor ativo e o legado (codigo gerado + simulacao) ou o
// nativo (um adaptador guest_abi.h sobre native/).
//
// A troca usa o mesmo mecanismo dos mods (runtime/mods): com slots (build BS_MODS), vale para
// chamadas diretas e indiretas; sem slots, so para as indiretas, e select() informa isso.
// Voltar ao legado restaura o provedor que estava ativo antes (um mod do usuario continua);
// se um mod trocou a funcao enquanto o nativo estava ativo, o mod fica.
// No N0 nenhum subsistema tem provedor nativo: tudo fica no legado.
#include "cpu/sh4_runtime.h"
#include "dc_regions.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace bs::transitional {

enum class Provider : uint8_t { Legacy, Native };
const char* provider_name(Provider p);

struct BoundaryInfo {
    uint32_t         address   = 0;
    dcmap::Subsystem subsystem = dcmap::Subsystem::None;
    std::string      name;
    Sh4Func          native    = nullptr;  // adaptador (nullptr = sem provedor nativo ainda)
    Sh4Func          previous  = nullptr;  // provedor ativo antes do nativo (mod do usuario ou gerado)
    Provider         active    = Provider::Legacy;
};

enum class SelectResult : uint8_t {
    Ok,            // provedor trocado para chamadas diretas e indiretas
    IndirectOnly,  // sem slot: so as chamadas indiretas usam o novo provedor
    NoNative,      // pediu nativo sem adaptador registrado
    Unknown,       // endereco nao registrado ou sem funcao recompilada
};

class BoundaryRegistry {
public:
    bool add(uint32_t address, dcmap::Subsystem subsystem, std::string name);  // false se repetido
    bool set_native(uint32_t address, Sh4Func adapter);
    SelectResult select(uint32_t address, Provider p);
    Provider     active(uint32_t address) const;
    size_t       count(Provider p) const;
    size_t       count(Provider p, dcmap::Subsystem s) const;
    // Volta tudo ao legado (fim de sessao, testes).
    void reset();
    const std::map<uint32_t, BoundaryInfo>& entries() const { return entries_; }

private:
    std::map<uint32_t, BoundaryInfo> entries_;  // por endereco fisico
};

BoundaryRegistry& boundary_registry();

} // namespace bs::transitional
