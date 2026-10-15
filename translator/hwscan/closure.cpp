#include "closure.h"

#include <deque>

namespace bs::hwscan {

const char* boundary_kind_name(BoundaryKind k) { return k == BoundaryKind::Direct ? "direta" : "indireta"; }

namespace {

bool isSdkSide(Layer l) { return l == Layer::Sdk || l == Layer::Unknown || l == Layer::Conflict; }

Layer layerOf(const SdkId& id, uint32_t fn) {
    const auto it = id.functions.find(fn);
    return it == id.functions.end() ? Layer::Unknown : it->second.layer;
}

} // namespace

std::set<uint32_t> indirect_targets(const Scan& s, const SdkId& id) {
    std::set<uint32_t> out;
    for (const auto& [fn, by] : s.takenBy) {
        if (!id.functions.count(fn)) continue;
        const bool gameFn = layerOf(id, fn) == Layer::Game;
        for (uint32_t r : by)
            if ((r && layerOf(id, r) == Layer::Game) || (!r && gameFn)) {
                out.insert(fn);
                break;
            }
    }
    return out;
}

std::set<uint32_t> data_only_sdk_targets(const Scan& s, const SdkId& id) {
    std::set<uint32_t> out;
    for (const auto& [fn, by] : s.takenBy)
        if (id.functions.count(fn) && isSdkSide(layerOf(id, fn)) && by.count(0)) out.insert(fn);
    return out;
}

std::map<uint32_t, std::set<dcmap::Subsystem>> hw_reach(const Scan& s) {
    std::map<uint32_t, std::set<dcmap::Subsystem>> reach;
    for (const auto& r : s.refs)
        if (r.function) reach[r.function].insert(dcmap::region_subsystem(r.region));
    // `pref` de endereco calculado numa funcao que tambem carrega a base das store queues: uso
    // provavel de SQ (envio ao TA), contado como render de forma conservadora (revisao Codex)
    for (const auto& [fn, n] : s.prefUnresolved)
        if (s.sqLiteral.count(fn)) reach[fn].insert(dcmap::Subsystem::Render);
    // ponto fixo sobre as arestas diretas (o grafo tem ciclos; conjuntos so crescem)
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& e : s.edges) {
            if (!e.callee || e.kind == EdgeKind::Indirect) continue;
            const auto it = reach.find(e.callee);
            if (it == reach.end()) continue;
            auto& dst = reach[e.caller];
            for (auto sub : reach[e.callee])
                if (dst.insert(sub).second) changed = true;
        }
    }
    return reach;
}

std::map<uint32_t, BoundaryEntry> derive_boundary(const Scan& s, const SdkId& id) {
    const auto                        reach = hw_reach(s);
    std::map<uint32_t, BoundaryEntry> b;
    auto add = [&](uint32_t fn, BoundaryKind k) {
        auto& e = b[fn];
        if (!e.address) {
            e.address = fn;
            e.kind    = k;
            if (auto it = id.functions.find(fn); it != id.functions.end()) {
                e.family = it->second.family;
                e.name   = it->second.name;
            }
            if (auto it = reach.find(fn); it != reach.end()) e.subsystems = it->second;
        }
        return &e;
    };
    for (const auto& e : s.edges) {
        if (!e.callee || e.kind == EdgeKind::Indirect) continue;
        if (layerOf(id, e.caller) != Layer::Game || !isSdkSide(layerOf(id, e.callee))) continue;
        if (!id.functions.count(e.callee)) continue;
        if (e.kind == EdgeKind::Dynamic) add(e.callee, BoundaryKind::Indirect)->observed = true;
        else add(e.callee, BoundaryKind::Direct)->callSites++;
    }
    for (const auto& [fn, by] : s.takenBy) {
        if (!id.functions.count(fn) || !isSdkSide(layerOf(id, fn)) || b.count(fn)) continue;
        for (uint32_t r : by)
            if (r && layerOf(id, r) == Layer::Game) {
                add(fn, BoundaryKind::Indirect);
                break;
            }
    }
    return b;
}

std::map<uint32_t, std::set<uint32_t>> hw_dominance(const Scan& s, const SdkId& id, const std::set<uint32_t>& boundary) {
    std::map<uint32_t, std::vector<uint32_t>> callees;
    for (const auto& e : s.edges)
        if (e.callee && e.kind != EdgeKind::Indirect) callees[e.caller].push_back(e.callee);
    std::map<uint32_t, std::set<uint32_t>> out;
    for (const auto& r : s.refs)
        if (r.function) out[r.function];
    for (uint32_t b : boundary) {
        std::set<uint32_t>    seen{b};
        std::vector<uint32_t> work{b};
        while (!work.empty()) {
            const uint32_t fn = work.back();
            work.pop_back();
            if (auto it = out.find(fn); it != out.end()) it->second.insert(b);
            if (auto it = callees.find(fn); it != callees.end())
                for (uint32_t c : it->second)
                    if (layerOf(id, c) != Layer::Game && seen.insert(c).second) work.push_back(c);
        }
    }
    return out;
}

ClosureResult check_closure(const Scan& s, const SdkId& id, const std::set<uint32_t>& boundary) {
    std::map<uint32_t, std::vector<uint32_t>> callees;
    std::set<uint32_t>                        indirectCallers, hwFuncs;
    for (const auto& e : s.edges) {
        if (e.kind == EdgeKind::Indirect || !e.callee) indirectCallers.insert(e.caller);
        else callees[e.caller].push_back(e.callee);
    }
    for (const auto& r : s.refs)
        if (r.function) hwFuncs.insert(r.function);

    ClosureResult               res;
    std::map<uint32_t, uint32_t> from;  // funcao -> quem a alcancou
    std::set<uint32_t>          viaInd;
    std::deque<uint32_t>        work;
    for (const auto& [fn, f] : id.functions)
        if (f.layer == Layer::Game) {
            from.emplace(fn, 0);
            work.push_back(fn);
        }
    bool indirectOpened = false;
    auto visit          = [&](uint32_t fn, uint32_t parent, bool indirect) {
        if (boundary.count(fn) || from.count(fn)) return;
        from.emplace(fn, parent);
        if (indirect) viaInd.insert(fn);
        work.push_back(fn);
    };
    while (!work.empty()) {
        const uint32_t fn = work.front();
        work.pop_front();
        // funcao do SDK fora da fronteira: e a violacao; o que vem atras dela nao e expandido
        if (layerOf(id, fn) != Layer::Game && isSdkSide(layerOf(id, fn))) continue;
        if (auto it = callees.find(fn); it != callees.end())
            for (uint32_t c : it->second) visit(c, fn, false);
        if (indirectCallers.count(fn) && !indirectOpened) {
            // Uma chamada indireta alcancada abre os alvos que o jogo pode ter nas maos: funcoes
            // cujo endereco o codigo do jogo toma, e funcoes do jogo com endereco em dado. Funcoes
            // do SDK com endereco so em dado (dono da tabela desconhecido) nao tem prova nem de
            // alcance nem de nao alcance: ficam como "indireto nao resolvido" (revisao Codex
            // 2026-10-15), contadas e com catraca, ate uma analise de points-to ou a sonda.
            indirectOpened = true;
            for (uint32_t t : indirect_targets(s, id)) visit(t, fn, true);
            for (uint32_t t : data_only_sdk_targets(s, id))
                if (!boundary.count(t) && !from.count(t)) res.unresolved.push_back(t);
        }
    }
    res.reached = from.size();
    for (const auto& [fn, parent] : from) {
        const Layer l = layerOf(id, fn);
        if (l != Layer::Game && isSdkSide(l))
            res.violations.push_back({Violation::SdkBypass, fn, parent, viaInd.count(fn) > 0});
        if (hwFuncs.count(fn)) res.violations.push_back({Violation::HardwareAccess, fn, parent, viaInd.count(fn) > 0});
    }
    return res;
}

} // namespace bs::hwscan
