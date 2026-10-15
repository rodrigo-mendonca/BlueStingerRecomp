#include "boundary_registry.h"

#include "mods/mod_api.h"

namespace bs::transitional {

const char* provider_name(Provider p) { return p == Provider::Legacy ? "legado" : "nativo"; }

bool BoundaryRegistry::add(uint32_t address, dcmap::Subsystem subsystem, std::string name) {
    const uint32_t a = sh4_phys(address);
    if (entries_.count(a)) return false;
    entries_[a] = {address, subsystem, std::move(name), nullptr, nullptr, Provider::Legacy};
    return true;
}

bool BoundaryRegistry::set_native(uint32_t address, Sh4Func adapter) {
    const auto it = entries_.find(sh4_phys(address));
    if (it == entries_.end() || !adapter) return false;
    it->second.native = adapter;
    return true;
}

SelectResult BoundaryRegistry::select(uint32_t address, Provider p) {
    const auto it = entries_.find(sh4_phys(address));
    if (it == entries_.end()) return SelectResult::Unknown;
    BoundaryInfo& e = it->second;
    if (p == Provider::Native && !e.native) return SelectResult::NoNative;
    if (p == Provider::Legacy) {
        // so desfaz a troca feita por este registro, voltando ao provedor anterior (gerado ou
        // mod do usuario); uma substituicao de mod feita depois fica (revisao Codex 2026-10-15)
        if (e.active == Provider::Native && sh4_lookup(e.address) == e.native) {
            const Sh4Func gen  = sh4_lookup_generated(e.address);
            const Sh4Func back = e.previous && e.previous != gen ? e.previous : nullptr;
            if (!mods::replace(e.address, back)) return SelectResult::Unknown;
        }
        e.previous = nullptr;
    } else {
        const Sh4Func cur = sh4_lookup(e.address);
        if (e.active != Provider::Native) e.previous = cur;
        if (!mods::replace(e.address, e.native)) return SelectResult::Unknown;
    }
    e.active = p;
    if (p == Provider::Legacy) return SelectResult::Ok;
    return mods::has_slot(e.address) ? SelectResult::Ok : SelectResult::IndirectOnly;
}

Provider BoundaryRegistry::active(uint32_t address) const {
    const auto it = entries_.find(sh4_phys(address));
    return it == entries_.end() ? Provider::Legacy : it->second.active;
}

size_t BoundaryRegistry::count(Provider p) const {
    size_t n = 0;
    for (const auto& [a, e] : entries_) n += e.active == p;
    return n;
}

size_t BoundaryRegistry::count(Provider p, dcmap::Subsystem s) const {
    size_t n = 0;
    for (const auto& [a, e] : entries_) n += e.active == p && e.subsystem == s;
    return n;
}

void BoundaryRegistry::reset() {
    for (auto& [a, e] : entries_)
        if (e.active == Provider::Native) select(e.address, Provider::Legacy);
}

BoundaryRegistry& boundary_registry() {
    static BoundaryRegistry r;
    return r;
}

} // namespace bs::transitional
