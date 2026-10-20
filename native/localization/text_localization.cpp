#include "localization/text_localization.h"

#include "localization/locale.h"

#include <toml++/toml.hpp>

#include <algorithm>

namespace bs::native::localization {

TextLocalization TextLocalization::load(const ModPackages& mods, std::string_view language) {
    TextLocalization out;
    for (const std::string& lg : language_fallback(language)) {
        for (const ModPackage& p : mods.packages()) {  // ja em ordem de prioridade
            const auto f = safe_join(p.dir, "textos/" + lg + ".toml");
            std::error_code ec;
            if (!f || !std::filesystem::is_regular_file(*f, ec)) continue;
            toml::table t;
            try {
                t = toml::parse_file(f->u8string());
            } catch (const toml::parse_error& e) {
                out.problems_.push_back(p.id + ": textos/" + lg + ".toml ilegivel: " + std::string(e.description()));
                continue;
            }
            const auto* tab = t["textos"].as_table();
            if (!tab) continue;
            for (const auto& [k, v] : *tab) {
                const std::string id(k.str());
                const auto        s = v.value<std::string>();
                if (!valid_resource_id(id)) { out.problems_.push_back(p.id + ": ID invalido '" + id + "'"); continue; }
                if (!s || !valid_utf8(*s)) { out.problems_.push_back(p.id + ": texto de '" + id + "' nao e texto UTF-8"); continue; }
                out.texts_.try_emplace(id, Entry{*s, p.id, lg});  // o primeiro (idioma exato, maior prioridade) fica
            }
        }
    }
    return out;
}

std::optional<std::string> TextLocalization::translate(std::string_view id, std::string_view original, const Tokenizer& codes) const {
    const auto it = texts_.find(id);
    if (it == texts_.end()) return std::nullopt;
    if (codes) {
        auto a = codes(original), b = codes(it->second.text);
        std::sort(a.begin(), a.end()), std::sort(b.begin(), b.end());
        if (a != b) {
            problems_.push_back(it->second.package + ": '" + std::string(id) + "' muda os codigos especiais do texto original; usando o original");
            return std::nullopt;
        }
    }
    return it->second.text;
}

} // namespace bs::native::localization
