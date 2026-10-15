#include "metrics.h"

#include <sstream>

namespace bs::hwscan {

Metrics compute_metrics(const Scan& s, const SdkId& id, const std::map<uint32_t, BoundaryEntry>& boundary,
                        const ClosureResult& closure, const std::map<uint32_t, std::string>& providers) {
    Metrics m;
    m["funcoes.total"] = int64_t(id.functions.size());
    for (const auto& [fn, f] : id.functions) {
        m[std::string("camada.") + layer_name(f.layer)]++;
        m[std::string("familia.") + family_name(f.family)]++;
    }
    for (const char* l : {"jogo", "sdk", "desconhecida", "conflito"}) m.try_emplace(std::string("camada.") + l, 0);

    // hw.*: analise estatica (deterministica, com catraca); sonda.*: execucao (informativo)
    m["hw.refs.total"] = 0;
    m.try_emplace("hw.refs.camada.jogo", 0);
    m.try_emplace("hw.refs.acessos", 0);
    m.try_emplace("hw.refs.constantes", 0);
    for (const auto& r : s.refs) {
        if (r.dynamic) {
            const auto  it = id.functions.find(r.function);
            const Layer l  = it == id.functions.end() ? Layer::Unknown : it->second.layer;
            m["sonda.acessos.total"]++;
            m[std::string("sonda.acessos.camada.") + layer_name(l)]++;
            m[std::string("sonda.subsistema.") + dcmap::subsystem_name(dcmap::region_subsystem(r.region))]++;
            continue;
        }
        m["hw.refs.total"]++;
        m[r.kind == Kind::Constant ? "hw.refs.constantes" : "hw.refs.acessos"]++;
        m[std::string("hw.subsistema.") + dcmap::subsystem_name(dcmap::region_subsystem(r.region))]++;
        if (!r.function) {
            m["hw.refs.dado"]++;
            continue;
        }
        const auto it = id.functions.find(r.function);
        if (it != id.functions.end() && it->second.layer == Layer::Game) m["hw.refs.camada.jogo"]++;
    }

    auto layerName = [&](uint32_t fn) {
        const auto it = id.functions.find(fn);
        return std::string(layer_name(it == id.functions.end() ? Layer::Unknown : it->second.layer));
    };
    for (const char* l : {"jogo", "sdk"}) {
        m.try_emplace(std::string("pref.nao_resolvido.funcoes.") + l, 0);
        m.try_emplace(std::string("sq.literal.funcoes.") + l, 0);
    }
    for (const auto& [fn, n] : s.prefUnresolved) {
        m["pref.nao_resolvido.funcoes." + layerName(fn)]++;
        m["pref.nao_resolvido.instrucoes"] += n;
    }
    for (const auto& [fn, n] : s.prefUnresolved)
        if (s.sqLiteral.count(fn)) m["pref.sq_provavel.funcoes"]++;
    for (const auto& [fn, n] : s.sqLiteral) {
        m["sq.literal.funcoes." + layerName(fn)]++;
        m["sq.literal.cargas"] += n;
    }

    m["fronteira.total"] = int64_t(boundary.size());
    m.try_emplace("fronteira.direta", 0);
    m.try_emplace("fronteira.indireta", 0);
    m.try_emplace("fronteira.provedor.legado", 0);
    m.try_emplace("fronteira.provedor.nativo", 0);
    for (const auto& [addr, e] : boundary) {
        m[std::string("fronteira.") + boundary_kind_name(e.kind)]++;
        const auto        p    = providers.find(addr);
        const std::string prov = p == providers.end() ? "legado" : p->second;
        m["fronteira.provedor." + prov]++;
        for (auto sub : e.subsystems) {
            m[std::string("fronteira.subsistema.") + dcmap::subsystem_name(sub)]++;
            if (prov == "legado") m[std::string("legado.subsistema.") + dcmap::subsystem_name(sub)]++;
        }
    }

    m["indireto.sdk_so_em_dado"] = int64_t(data_only_sdk_targets(s, id).size());
    for (const auto& e : s.edges) {
        if (e.kind != EdgeKind::Dynamic) continue;
        const auto fa = id.functions.find(e.caller), fb = id.functions.find(e.callee);
        const Layer la = fa == id.functions.end() ? Layer::Unknown : fa->second.layer;
        const Layer lb = fb == id.functions.end() ? Layer::Unknown : fb->second.layer;
        m["sonda.indiretas.total"]++;
        if (la == Layer::Game && lb != Layer::Game) m["sonda.indiretas.jogo_para_sdk"]++;
        if (la != Layer::Game && lb == Layer::Game) m["sonda.indiretas.sdk_para_jogo"]++;
        if (fb == id.functions.end()) m["sonda.indiretas.fora_da_imagem"]++;
    }
    m["fechamento.alcancadas"] = int64_t(closure.reached);
    m.try_emplace("fechamento.violacoes.sdk", 0);
    m.try_emplace("fechamento.violacoes.hw", 0);
    m["fechamento.indireto_nao_resolvido"] = int64_t(closure.unresolved.size());
    for (const auto& v : closure.violations)
        m[v.kind == Violation::SdkBypass ? "fechamento.violacoes.sdk" : "fechamento.violacoes.hw"]++;
    return m;
}

const std::vector<std::string>& ratchet_keys() {
    static const std::vector<std::string> k = {
        "hw.refs.camada.jogo",        // jogo acessando hardware diretamente
        "fechamento.violacoes.sdk",   // chamadas que contornam a fronteira declarada
        "fechamento.violacoes.hw",    // hardware alcancado sem passar pela fronteira
        "fechamento.indireto_nao_resolvido",  // alvos indiretos possiveis sem prova (so em dado)
        "camada.desconhecida",        // funcoes sem camada atribuida
        "pref.nao_resolvido.funcoes.jogo",  // funcoes do jogo com pref de endereco calculado
        "sq.literal.funcoes.jogo",          // funcoes do jogo com a base das store queues
        "camada.conflito",
        "fronteira.provedor.legado",  // fronteira ainda servida pelo runtime legado
    };
    return k;
}

std::vector<RatchetFailure> ratchet(const Metrics& base, const Metrics& now) {
    std::vector<RatchetFailure> out;
    for (const auto& k : ratchet_keys()) {
        const auto b = base.find(k);
        const auto n = now.find(k);
        const int64_t bv = b == base.end() ? 0 : b->second, nv = n == now.end() ? 0 : n->second;
        if (b != base.end() && nv > bv) out.push_back({k, bv, nv});
    }
    return out;
}

std::string metrics_toml(const Metrics& m) {
    std::ostringstream o;
    o << "# Metricas de dependencia do Dreamcast (bs-hwscan). Chaves com catraca: nao podem subir.\n";
    for (const auto& k : ratchet_keys()) o << "# catraca: " << k << "\n";
    for (const auto& [k, v] : m) o << '"' << k << "\" = " << v << "\n";
    return o.str();
}

Metrics parse_metrics(const std::string& text) {
    Metrics            m;
    std::istringstream in(text);
    std::string        line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] != '"') continue;
        const size_t q = line.find('"', 1);
        const size_t eq = line.find('=', q == std::string::npos ? 0 : q);
        if (q == std::string::npos || eq == std::string::npos) continue;
        try {
            m[line.substr(1, q - 1)] = std::stoll(line.substr(eq + 1));
        } catch (...) {
        }
    }
    return m;
}

} // namespace bs::hwscan
