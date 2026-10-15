#include "boundary_io.h"

#include <toml++/toml.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace bs::hwscan {

namespace {

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

Family familyByName(const std::string& n) {
    for (int i = 0; i <= int(Family::Crt); ++i)
        if (n == family_name(Family(i))) return Family(i);
    return Family::Unknown;
}

} // namespace

std::string boundary_toml(const std::map<uint32_t, BoundaryEntry>& b, const std::map<uint32_t, std::string>& providers) {
    std::ostringstream o;
    o << "# Fronteira declarada entre o jogo e o SDK/hardware (N0, docs/N0-RELATORIO.md).\n"
         "# Gerada por bs-hwscan --fronteira-gerar; conferida por bs-hwscan --fronteira (teste\n"
         "# n0_fechamento). Uma chamada do jogo para fora desta lista e uma violacao.\n"
         "# provedor: \"legado\" (runtime atual) ou \"nativo\" (subsistema N1-N9 ja migrado).\n";
    for (const auto& [addr, e] : b) {
        const auto p = providers.find(addr);
        o << "\n[[fronteira]]\nendereco = " << hex(addr) << "\ntipo = \"" << boundary_kind_name(e.kind)
          << "\"\nfamilia = \"" << family_name(e.family) << "\"\nsubsistemas = [";
        bool first = true;
        for (auto sub : e.subsystems) {
            o << (first ? "" : ", ") << '"' << dcmap::subsystem_name(sub) << '"';
            first = false;
        }
        o << "]\nprovedor = \"" << (p == providers.end() ? "legado" : p->second) << "\"\n";
        if (!e.name.empty()) o << "nome = \"" << e.name << "\"\n";
        if (e.callSites) o << "chamadas = " << e.callSites << "\n";
        if (e.observed) o << "observada = true\n";
    }
    return o.str();
}

DeclaredBoundary parse_boundary(const std::string& text) {
    DeclaredBoundary d;
    toml::table      t;
    try {
        t = toml::parse(text);
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(std::string("fronteira: TOML invalido: ") + std::string(e.description()));
    }
    const toml::array* arr = t["fronteira"].as_array();
    if (!arr) return d;
    for (const auto& node : *arr) {
        const toml::table* e = node.as_table();
        if (!e) throw std::runtime_error("fronteira: entrada nao e tabela");
        const auto addr = (*e)["endereco"].value<int64_t>();
        if (!addr || *addr <= 0 || *addr > 0xFFFFFFFFll) throw std::runtime_error("fronteira: endereco ausente/invalido");
        BoundaryEntry be;
        be.address = uint32_t(*addr);
        be.kind    = (*e)["tipo"].value_or(std::string("direta")) == "indireta" ? BoundaryKind::Indirect : BoundaryKind::Direct;
        be.family  = familyByName((*e)["familia"].value_or(std::string()));
        be.name    = (*e)["nome"].value_or(std::string());
        be.callSites = int((*e)["chamadas"].value_or(int64_t(0)));
        be.observed  = (*e)["observada"].value_or(false);
        if (const toml::array* subs = (*e)["subsistemas"].as_array())
            for (const auto& sn : *subs)
                for (int i = 0; i <= int(dcmap::Subsystem::System); ++i)
                    if (sn.value_or(std::string()) == dcmap::subsystem_name(dcmap::Subsystem(i)))
                        be.subsystems.insert(dcmap::Subsystem(i));
        const std::string prov = (*e)["provedor"].value_or(std::string("legado"));
        if (prov != "legado" && prov != "nativo") throw std::runtime_error("fronteira: provedor invalido em " + hex(be.address));
        if (d.entries.count(be.address)) throw std::runtime_error("fronteira: endereco repetido " + hex(be.address));
        d.providers[be.address] = prov;
        d.entries[be.address]   = be;
    }
    return d;
}

DeclaredBoundary load_boundary(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("fronteira: nao foi possivel abrir o arquivo");
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_boundary(ss.str());
}

} // namespace bs::hwscan
