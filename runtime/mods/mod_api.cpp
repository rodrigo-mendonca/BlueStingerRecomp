#include "mods/mod_api.h"

#include <algorithm>
#include <set>
#include <cstdio>
#include <stdexcept>

namespace bs::mods {
namespace {

std::vector<Slot>& slots() {
    static std::vector<Slot> s;
    return s;
}

struct Entry {
    const char* name;
    ModInit     init;
};
std::vector<Entry>& mods() {  // construida no primeiro uso (registro estatico de varios .cpp)
    static std::vector<Entry> m;
    return m;
}

// todos os enderecos com substituicao indireta (inclusive sem slot): reset() limpa todos
std::set<uint32_t>& overridden() {
    static std::set<uint32_t> s;
    return s;
}

Slot* find(uint32_t addr) {
    const uint32_t p = sh4_phys(addr);
    auto&          s = slots();
    auto it = std::lower_bound(s.begin(), s.end(), p, [](const Slot& e, uint32_t v) { return sh4_phys(e.addr) < v; });
    return it != s.end() && sh4_phys(it->addr) == p ? &*it : nullptr;
}

} // namespace

void set_slots(const Slot* s, size_t count) {
    slots().assign(s, s + count);
    std::sort(slots().begin(), slots().end(), [](const Slot& a, const Slot& b) { return sh4_phys(a.addr) < sh4_phys(b.addr); });
}

bool replace(uint32_t addr, Sh4Func fn) {
    Sh4Func orig = nullptr;
    if (Slot* s = find(addr)) {
        *s->slot = fn ? fn : s->original;  // chamadas diretas
        orig     = s->original;
    } else {
        orig = sh4_lookup(addr);  // build sem BS_MODS: so as indiretas mudam
        if (!orig) return false;
    }
    const bool on = fn && fn != orig;
    sh4_override(addr, on ? fn : nullptr);  // chamadas indiretas
    if (on) overridden().insert(sh4_phys(addr));
    else overridden().erase(sh4_phys(addr));
    return true;
}

Sh4Func original(uint32_t addr) {
    if (const Slot* s = find(addr)) return s->original;
    return nullptr;
}

void call_original(uint32_t addr, Sh4Context* c) {
    const Sh4Func f = original(addr);
    if (!f) throw std::runtime_error("mods: nao ha funcao original no endereco pedido");
    f(c);
}

void reset() {
    for (Slot& s : slots()) *s.slot = s.original;
    for (uint32_t a : overridden()) sh4_override(a, nullptr);  // inclusive enderecos sem slot (revisao Codex)
    overridden().clear();
}

ModReg::ModReg(const char* name, ModInit init) { mods().push_back({name, init}); }

void init_all() {
    for (const Entry& m : mods()) {
        std::fprintf(stderr, "[mods] iniciando %s\n", m.name);
        m.init();
    }
}

std::vector<std::string> registered() {
    std::vector<std::string> r;
    for (const Entry& m : mods()) r.push_back(m.name);
    return r;
}

} // namespace bs::mods
