#include "sdkid.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <set>

namespace bs::hwscan {

const char* family_name(Family f) {
    switch (f) {
    case Family::Unknown: return "desconhecida";
    case Family::Game: return "jogo";
    case Family::Shinobi: return "shinobi";
    case Family::Kamui: return "kamui";
    case Family::Ninja: return "ninja";
    case Family::Cri: return "cri";
    case Family::Sound: return "som_sd";
    case Family::Bios: return "bios";
    case Family::Crt: return "runtime_c";
    }
    return "?";
}

const char* layer_name(Layer l) {
    switch (l) {
    case Layer::Unknown: return "desconhecida";
    case Layer::Game: return "jogo";
    case Layer::Sdk: return "sdk";
    case Layer::Conflict: return "conflito";
    }
    return "?";
}

const char* confidence_name(Confidence c) {
    switch (c) {
    case Confidence::None: return "-";
    case Confidence::Low: return "baixa";
    case Confidence::Medium: return "media";
    case Confidence::High: return "alta";
    }
    return "?";
}

namespace {

bool identChar(char c) { return std::isalnum(uint8_t(c)) || c == '_'; }

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Familia pelo prefixo do identificador (convencoes de nome do SDK Katana e da CRI).
Family familyOfIdent(const std::string& id) {
    if (id.size() < 3) return Family::Unknown;
    auto lowerThenUpper = [&](size_t n) { return id.size() > n && std::isupper(uint8_t(id[n])); };
    if (startsWith(id, "mw") && lowerThenUpper(2)) return Family::Cri;
    for (const char* p : {"ADX", "adx", "Ply", "Exec", "SJ", "MWRNA", "MWSJ", "GSC", "MPV", "MPS", "SFD", "Sfd"})
        if (startsWith(id, p)) return Family::Cri;
    if (startsWith(id, "sd") && (lowerThenUpper(2) || id[2] == '_')) return Family::Sound;
    if (startsWith(id, "nj") && lowerThenUpper(2)) return Family::Ninja;
    if (startsWith(id, "km") && lowerThenUpper(2)) return Family::Kamui;
    for (const char* p : {"sy", "gd", "pd", "bu", "fm", "kd"})
        if (startsWith(id, p) && lowerThenUpper(2)) return Family::Shinobi;
    // codigo do jogo: FI_ (arquivos), FT_ (cenario), FY_ (personagens) etc.
    if (id.size() > 3 && id[0] == 'F' && std::isupper(uint8_t(id[1])) && id[2] == '_') return Family::Game;
    return Family::Unknown;
}

std::string identAt(const std::string& s, size_t pos) {
    size_t e = pos;
    while (e < s.size() && identChar(s[e])) ++e;
    return s.substr(pos, e - pos);
}

} // namespace

Family owner_of(const std::string& textIn, std::string* name) {
    std::string text = textIn;
    while (!text.empty() && (text.back() == ' ' || text.back() == '.' || text.back() == '!')) text.pop_back();
    auto found = [&](const std::string& id) {
        const Family f = familyOfIdent(id);
        if (f != Family::Unknown && name) *name = id;
        return f;
    };
    // "... return error in mwSndOpenPort"
    if (const size_t p = text.rfind(" in "); p != std::string::npos) {
        const std::string id = identAt(text, p + 4);
        if (p + 4 + id.size() == text.size())
            if (const Family f = found(id); f != Family::Unknown) return f;
    }
    // "... (ADXT_SetOutPan)" / "(adxstmf_stat_exec)"
    if (!text.empty() && text.back() == ')') {
        const size_t open = text.rfind('(');
        if (open != std::string::npos) {
            const std::string id = identAt(text, open + 1);
            if (open + 1 + id.size() + 1 == text.size())
                if (const Family f = found(id); f != Family::Unknown) return f;
        }
    }
    // "E2003 mwSfdStop:...", "W2151 PlyCreateWav ...", "FT_LoadRoomData (): ...", "sd_drv_get_info_.c"
    size_t     p     = 0;
    const bool coded = text.size() > 5 && (text[0] == 'E' || text[0] == 'W') && std::isdigit(uint8_t(text[1]));
    if (coded) {
        while (p < text.size() && text[p] != ' ' && text[p] != ':') ++p;
        while (p < text.size() && (text[p] == ' ' || text[p] == ':')) ++p;
    }
    const std::string id = identAt(text, p);
    if (const Family f = found(id); f != Family::Unknown) {
        // No comeco, conta como dono seguido de '(' ou ':' ("FT_LoadRoomData (): ..." tambem),
        // como nome de fonte ("sd_drv_get_info_.c") ou, com codigo de erro, seguido de espaco
        // ("W2151 PlyCreateWav ..."). Sem codigo, "buSaveFile NO_DISK" e o jogo relatando o
        // resultado de uma chamada, nao a funcao chamada.
        size_t q = p + id.size();
        while (q < text.size() && text[q] == ' ' && !coded) ++q;
        const char next = q < text.size() ? text[q] : '\0';
        if (next == '(' || next == ':' || (coded && next == ' ') ||
            (next == '.' && text.compare(q, std::string::npos, ".c") == 0))
            return f;
    }
    if (name) name->clear();
    // marcadores das bibliotecas (sem dono nomeado)
    if (text == "KAMUI" || text.find("(C) NEC") != std::string::npos) return Family::Kamui;
    if (text == "KATANA_FLASH____") return Family::Shinobi;
    if (text.find("MWRNA") != std::string::npos || text.find("Version 1.04 1998") != std::string::npos ||
        startsWith(text, "mwMng:"))
        return Family::Cri;
    return Family::Unknown;
}

Family family_of_module(const std::string& n) {
    if (n == "Ninja" || n == "SEGA Ninja Library") return Family::Ninja;
    if (n == "KAMUI") return Family::Kamui;
    if (n == "sd") return Family::Sound;
    if (n == "Shinobi" || n == "Shinobi Library" || n == "Dreamcast" || n == "bu" || n == "kd" || n == "pd")
        return Family::Shinobi;
    for (const char* p : {"sy", "gd", "pd", "fm"})
        if (startsWith(n, p) && n.size() > 2 && std::isupper(uint8_t(n[2]))) return Family::Shinobi;
    if (startsWith(n, "mw") || startsWith(n, "MW") || startsWith(n, "ADX") || startsWith(n, "CRI") || n == "SJ" ||
        n == "MPV" || n == "MPS" || n == "SFD" || n == "wav")
        return Family::Cri;
    return Family::Unknown;
}

std::vector<Module> find_modules(const analysis::Image& img) {
    std::vector<Module> mods;
    const auto&         b = img.bytes;
    auto printable = [](uint8_t c) { return c >= 0x20 && c < 0x7F; };
    for (size_t i = 0; i < b.size();) {
        if (!printable(b[i])) { ++i; continue; }
        size_t e = i;
        while (e < b.size() && printable(b[e])) ++e;
        const std::string run(b.begin() + i, b.begin() + e);
        // "syHw Ver 1.03 Build:...", "mwRna Ver.1.07 Build:...", "MWRNA Version 1.07 ..."
        size_t      v = run.find(" Ver");
        std::string name;
        size_t      at = 0;
        if (v != std::string::npos && v + 4 < run.size() &&
            (run[v + 4] == ' ' || run[v + 4] == '.' || run.compare(v + 4, 4, "sion") == 0) &&
            (run.find("Build:") != std::string::npos || run.compare(v + 4, 5, "sion ") == 0)) {
            size_t s = v;
            while (s > 0 && identChar(run[s - 1])) --s;
            name = run.substr(s, v - s);
            at   = s;
        }
        for (const char* marker : {"SEGA Ninja Library", "KAMUI"})
            if (name.empty() && run.size() >= std::strlen(marker) &&
                run.compare(run.size() - std::strlen(marker), std::string::npos, marker) == 0 &&
                (e < b.size() && b[e] == 0))
                name = marker, at = run.size() - std::strlen(marker);
        if (!name.empty()) {
            Module m;
            m.banner  = img.base + uint32_t(i + at);
            m.dataEnd = img.base + uint32_t(e);
            m.name   = name;
            m.text   = run.substr(at, 60);
            m.family = family_of_module(name);
            mods.push_back(m);
        }
        i = e;
    }
    for (size_t k = 0; k < mods.size(); ++k) {
        mods[k].dataBegin = k > 0 ? mods[k - 1].dataEnd : img.base;
        mods[k].bounded   = mods[k].dataEnd - mods[k].dataBegin <= 2048;
    }
    return mods;
}

namespace {

// Familia que controla o dispositivo (so dispositivos de dono unico; CCN, cache, store
// queues, DMAC e interrupcoes do Holly sao usados por varias bibliotecas e pelo jogo).
Family familyOfRegion(dcmap::Region r) {
    using dcmap::Region;
    switch (r) {
    case Region::Pvr: case Region::PvrDma: case Region::TaFifo: case Region::TaYuv: case Region::TaTexture:
    case Region::Vram64: case Region::Vram32: return Family::Kamui;
    case Region::AicaRegs: case Region::AicaRam: return Family::Sound;
    case Region::Maple: case Region::GdRom: case Region::G1: case Region::Flash: case Region::Tmu: case Region::Rtc:
    case Region::AicaRtc: case Region::Ubc: case Region::Bsc: case Region::Cpg: case Region::Sci: case Region::Scif:
        return Family::Shinobi;
    case Region::BiosVectors: case Region::BiosGdVector: case Region::BiosFlashVector: return Family::Bios;
    default: return Family::Unknown;
    }
}

constexpr int kFamilies = 9;

} // namespace

SdkId identify(const analysis::Image& img, const analysis::Analysis& a, const Scan& s) {
    SdkId out;
    out.modules = find_modules(img);
    for (const auto& [entry, f] : a.functions) out.functions[entry];

    // Votos por funcao e por tipo de evidencia (cada tipo conta uma vez por familia):
    // nome 4, vetor do BIOS 4, modulo 2, hardware 2, faixa 2, grafo 1.
    struct Votes {
        std::map<std::string, std::set<Family>> byKind;
        int score(Family f) const {
            static const std::map<std::string, int> kWeight = {
                {"nome", 4}, {"bios", 4}, {"assinatura", 4}, {"padrao", 2}, {"modulo", 2}, {"hw", 2}, {"faixa", 2}, {"grafo", 1}};
            int w = 0;
            for (const auto& [k, fams] : byKind)
                if (fams.count(f)) w += kWeight.at(k);
            return w;
        }
        Family best() const {
            int bw = 0, bi = 0;
            for (int i = 1; i < kFamilies; ++i)
                if (const int w = score(Family(i)); w > bw) bw = w, bi = i;
            return Family(bi);
        }
    };
    std::map<uint32_t, Votes>  votes;
    std::map<uint32_t, Family> seed;
    auto vote = [&](uint32_t fn, Family f, const std::string& kind, const std::string& detail) {
        if (f == Family::Unknown) return;
        votes[fn].byKind[kind].insert(f);
        auto&             ev = out.functions[fn].evidence;
        const std::string e  = kind + ":" + detail;
        if (std::find(ev.begin(), ev.end(), e) == ev.end()) ev.push_back(e);
    };
    auto best = [&](uint32_t fn) {
        const auto it = votes.find(fn);
        return it == votes.end() ? Family::Unknown : it->second.best();
    };

    // 0) assinatura de codigo
    for (const auto& [entry, f] : a.functions) {
        int  div1 = 0;
        bool lcg = false, angle = false, calls = !f.direct.empty(), ftrv = false, fsca = false;
        for (uint32_t pc : f.insns) {
            const sh4::Insn in = sh4::decode(img.read16(pc));
            if (in.op == sh4::Op::DIV1) ++div1;
            if (in.op == sh4::Op::FTRV) ftrv = true;
            if (in.op == sh4::Op::FSCA) fsca = true;
            if (in.op == sh4::Op::BSR) calls = true;
            if (in.op == sh4::Op::MOVL_PC) {
                const uint32_t lit = sh4::target(in, pc);
                if (img.contains(lit, 4) && img.read32(lit) == 0x41C64E6Du) lcg = true;
            }
            // angulo do Ninja (Angle, 16 bits): extu.w r4,r4 ... lds r4,fpul; fsca
            if (in.op == sh4::Op::FSCA && pc - entry <= 8 && img.read16(entry) == 0x644D) angle = true;
        }
        for (uint32_t pc : f.slots) {
            const sh4::Op op = sh4::decode(img.read16(pc)).op;
            if (op == sh4::Op::FSCA && pc - entry <= 8 && img.read16(entry) == 0x644D) angle = true;
            if (op == sh4::Op::FTRV) ftrv = true;
            if (op == sh4::Op::FSCA) fsca = true;
        }
        if (div1 >= 8 && !calls) vote(entry, Family::Crt, "assinatura", "divisao");
        if (lcg) vote(entry, Family::Crt, "assinatura", "rand");
        if (angle) vote(entry, Family::Ninja, "assinatura", "angulo_fsca");
        // Ninja: primeiro argumento NJS_MATRIX* (NULL = matriz corrente), `tst r4,r4` na entrada,
        // e transformacao com XMTRX (ftrv) ou rotacao com fsca. Padrao, nao prova: peso menor.
        if (img.read16(entry) == 0x2448 && (ftrv || fsca)) vote(entry, Family::Ninja, "padrao", "matriz_ninja");
    }
    for (const auto& [fn, v] : votes) seed[fn] = v.best();

    // 1) nome
    for (const auto& [fn, list] : s.strings) {
        std::set<Family> fams;
        for (const auto& str : list) {
            std::string  name;
            const Family f = owner_of(str.text, &name);
            if (f == Family::Unknown) continue;
            fams.insert(f);
            auto& sf = out.functions[fn];
            if (!name.empty() && sf.name.empty()) sf.name = name;
            vote(fn, f, "nome", name.empty() ? str.text.substr(0, 24) : name);
        }
        if (fams.size() == 1) seed[fn] = *fams.begin();
    }
    // 2) modulo: dados do bloco pequeno de um modulo
    for (const auto& [fn, refs] : s.dataRefs) {
        std::set<Family> fams;
        for (uint32_t d : refs) {
            auto it = std::upper_bound(out.modules.begin(), out.modules.end(), d,
                                       [](uint32_t x, const Module& m) { return x < m.dataEnd; });
            if (it == out.modules.end()) continue;
            const Module& m = *it;
            if (!m.bounded || d < m.dataBegin || m.family == Family::Unknown) continue;
            fams.insert(m.family);
            vote(fn, m.family, "modulo", m.name);
        }
        if (fams.size() == 1 && !seed.count(fn)) seed[fn] = *fams.begin();
    }
    // 3) hardware de dono unico (vetores do BIOS: stubs de syscall)
    std::map<uint32_t, std::set<Family>> hwFams;
    for (const auto& r : s.refs) {
        if (!r.function) continue;
        const Family f = familyOfRegion(r.region);
        if (f != Family::Unknown) hwFams[r.function].insert(f);
    }
    for (const auto& [fn, fams] : hwFams) {
        for (Family f : fams) vote(fn, f, f == Family::Bios ? "bios" : "hw", f == Family::Bios ? "vetor" : family_name(f));
        if (fams.size() == 1 && !seed.count(fn)) seed[fn] = *fams.begin();
    }

    // 4) faixa: entre duas sementes da mesma familia (sem outra no meio)
    {
        // no maximo kMaxGap funcoes entre as duas sementes: lacunas maiores ficam sem faixa
        constexpr size_t           kMaxGap = 64;
        std::map<uint32_t, size_t> index;
        for (const auto& [entry, f] : a.functions) index.emplace(entry, index.size());
        std::vector<std::pair<uint32_t, Family>> seeds;
        for (const auto& [fn, f] : seed) seeds.emplace_back(fn, best(fn));
        for (const auto& [entry, f] : a.functions) {
            if (seed.count(entry)) continue;
            auto hi = std::lower_bound(seeds.begin(), seeds.end(), std::make_pair(entry, Family::Unknown));
            if (hi == seeds.begin() || hi == seeds.end()) continue;
            const auto lo = std::prev(hi);
            if (!index.count(lo->first) || !index.count(hi->first) || index[hi->first] - index[lo->first] > kMaxGap)
                continue;
            if (lo->second == hi->second && lo->second != Family::Bios && lo->second != Family::Unknown)
                vote(entry, lo->second, "faixa", family_name(lo->second));
        }
    }

    // 5) grafo: funcao sem familia, sem endereco tomado, cujos chamadores (>= 1) sao todos de uma familia
    std::map<uint32_t, std::set<uint32_t>> callers;
    for (const auto& e : s.edges)
        if (e.callee && e.kind != EdgeKind::Indirect) callers[e.callee].insert(e.caller);
    for (int pass = 0; pass < 8; ++pass) {
        std::map<uint32_t, Family> add;
        for (const auto& [fn, cs] : callers) {
            if (!out.functions.count(fn) || best(fn) != Family::Unknown || s.addressTaken.count(fn)) continue;
            Family common = Family::Unknown;
            bool   ok     = true;
            for (uint32_t c : cs) {
                const Family f = best(c);
                if (f == Family::Unknown || (common != Family::Unknown && f != common)) {
                    ok = false;
                    break;
                }
                common = f;
            }
            if (ok && common != Family::Unknown && common != Family::Bios) add[fn] = common;
        }
        if (add.empty()) break;
        for (const auto& [fn, f] : add) vote(fn, f, "grafo", family_name(f));
    }

    // decisao e confianca
    for (auto& [fn, sf] : out.functions) {
        const auto it = votes.find(fn);
        if (it == votes.end()) continue;
        const Family b = it->second.best();
        sf.family      = b;
        std::set<std::string> pro, contra;
        for (const auto& [k, fams] : it->second.byKind) {
            if (fams.count(b)) pro.insert(k);
            if (fams.size() > 1 || !fams.count(b)) contra.insert(k);
        }
        sf.conflict       = !contra.empty();
        size_t strongKinds = 0, structural = 0;
        for (const auto& k : pro) {
            if (k == "nome" || k == "bios" || k == "assinatura") ++strongKinds;
            else if (k != "grafo") ++structural;
        }
        const bool strong = strongKinds > 0;
        if (!sf.conflict && (strong || structural >= 2)) sf.confidence = Confidence::High;
        else if ((!sf.conflict && structural >= 1) || (strong && !contra.count("nome"))) sf.confidence = Confidence::Medium;
        else sf.confidence = Confidence::Low;
    }

    // camada
    std::map<uint32_t, std::set<uint32_t>> callees;
    for (const auto& e : s.edges)
        if (e.callee && e.kind != EdgeKind::Indirect && out.functions.count(e.callee)) callees[e.caller].insert(e.callee);
    // Sementes: familia com confianca media/alta apoiada por nome, modulo, vetor do BIOS ou
    // faixa. Acesso a hardware sozinho nao serve: o jogo tambem acessa hardware diretamente
    // (e isso e justamente o que o manifesto precisa mostrar).
    std::set<uint32_t> game, sdk;
    for (const auto& [fn, sf] : out.functions) {
        if (sf.confidence < Confidence::Medium || sf.family == Family::Unknown) continue;
        const auto& byKind = votes.at(fn).byKind;
        bool        ok     = false;
        for (const char* k : {"nome", "modulo", "bios", "faixa", "assinatura", "padrao"})
            if (auto it = byKind.find(k); it != byKind.end() && it->second.count(sf.family)) ok = true;
        if (!ok) continue;
        if (sf.family == Family::Game) game.insert(fn);
        else sdk.insert(fn);
    }
    // Para cima: chamador direto de funcao do jogo e do jogo, exceto sementes do SDK (que
    // ficam em conflito e nao propagam: a partida chama o main, mas quem chama a partida nao
    // vira jogo por isso).
    const std::set<uint32_t> sdkSeeds = sdk;
    std::set<uint32_t>       conflict;
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [caller, cs] : callees)
            if (!game.count(caller) && !conflict.count(caller))
                for (uint32_t c : cs)
                    if (game.count(c)) {
                        if (sdkSeeds.count(caller)) conflict.insert(caller);
                        else game.insert(caller);
                        changed = true;
                        break;
                    }
    }
    for (uint32_t c : conflict) game.insert(c);
    // Para baixo: chamado diretamente pelo SDK, sem entrar no que ja e do jogo. A excecao
    // conhecida a "biblioteca nao chama o jogo" e a partida do Shinobi chamando o main do
    // jogo; ela fica como conflito (semente do SDK que tambem chama o jogo).
    std::vector<uint32_t> work(sdk.begin(), sdk.end());
    while (!work.empty()) {
        const uint32_t fn = work.back();
        work.pop_back();
        if (auto it = callees.find(fn); it != callees.end())
            for (uint32_t c : it->second)
                if (!game.count(c) && sdk.insert(c).second) work.push_back(c);
    }
    for (auto& [fn, sf] : out.functions) {
        const bool g = game.count(fn) > 0, k = sdk.count(fn) > 0;
        sf.layer     = g && k ? Layer::Conflict : g ? Layer::Game : k ? Layer::Sdk : Layer::Unknown;
    }

    // Bloco de ligacao: o linker poe a partida do Shinobi, os objetos do jogo e depois as
    // bibliotecas, em blocos de codigo contiguos (lacunas < 32 KB entre funcoes). Dentro de um
    // bloco, uma funcao sem camada entre duas vizinhas com camada da mesma camada fica com ela;
    // depois da ultima funcao com camada do bloco, so se for SDK (bibliotecas no fim).
    // As trocas jogo<->SDK em cada bloco ficam no relatorio (intercalacao).
    {
        std::vector<uint32_t> entries;
        for (const auto& [fn, sf] : out.functions) entries.push_back(fn);
        size_t b = 0;
        while (b < entries.size()) {
            size_t e = b + 1;
            while (e < entries.size() && entries[e] - entries[e - 1] < 0x8000) ++e;
            std::vector<std::pair<size_t, Layer>> known;
            for (size_t i = b; i < e; ++i) {
                const Layer l = out.functions[entries[i]].layer;
                if (l == Layer::Game || l == Layer::Sdk) known.emplace_back(i, l);
            }
            int switches = 0;
            for (size_t k = 1; k < known.size(); ++k) switches += known[k].second != known[k - 1].second;
            out.blocks.push_back({entries[b], entries[e - 1], switches});
            size_t k = 0;
            for (size_t i = b; i < e; ++i) {
                while (k < known.size() && known[k].first < i) ++k;
                auto& sf = out.functions[entries[i]];
                if (sf.layer != Layer::Unknown) continue;
                const bool  hasLo = k > 0, hasHi = k < known.size();
                const Layer lo    = hasLo ? known[k - 1].second : Layer::Unknown;
                const Layer hi    = hasHi ? known[k].second : Layer::Unknown;
                Layer       l     = Layer::Unknown;
                if (hasLo && hasHi && lo == hi) l = lo;
                else if (hasLo && !hasHi && lo == Layer::Sdk) l = Layer::Sdk;
                if (l == Layer::Unknown) continue;
                sf.layer = l;
                sf.evidence.push_back(l == Layer::Game ? "bloco:jogo" : "bloco:sdk");
            }
            b = e;
        }
    }
    return out;
}

} // namespace bs::hwscan
