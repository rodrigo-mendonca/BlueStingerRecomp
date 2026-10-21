#include "localization/mod_packages.h"

#include "localization/locale.h"

#include <toml++/toml.hpp>

#include <algorithm>

namespace bs::native::localization {

namespace fs = std::filesystem;

std::optional<fs::path> safe_join(const fs::path& root, std::string_view relative) {
    if (relative.empty() || relative.find("..") != std::string_view::npos || relative.find(':') != std::string_view::npos ||
        relative.front() == '/' || relative.front() == '\\')
        return std::nullopt;
    std::error_code ec;
    const fs::path  base = fs::weakly_canonical(root, ec);
    if (ec) return std::nullopt;
    const fs::path p = fs::weakly_canonical(base / fs::path(std::u8string(relative.begin(), relative.end())), ec);
    if (ec) return std::nullopt;
    const auto rel = p.lexically_relative(base);  // juncao/link que sai da pasta: o canonico fica fora
    if (rel.empty() || rel.native().rfind(L"..", 0) == 0) return std::nullopt;
    return p;
}

ModPackages ModPackages::discover(const fs::path& modsDir, std::string_view gameVersion) {
    ModPackages     out;
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator(modsDir, ec))
        if (e.is_directory(ec) && fs::is_regular_file(e.path() / L"mod.toml", ec)) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());
    for (const fs::path& d : dirs) {
        auto fail = [&](std::string m) { out.problems_.push_back({d, std::move(m)}); };
        toml::table t;
        try {
            t = toml::parse_file((d / L"mod.toml").u8string());
        } catch (const toml::parse_error& e) {
            fail("mod.toml ilegivel: " + std::string(e.description()));
            continue;
        }
        const auto fmt = t["formato"].value<int64_t>();
        if (!fmt) { fail("mod.toml sem 'formato'"); continue; }
        if (*fmt > kModFormat) { fail("formato " + std::to_string(*fmt) + " e mais novo que o suportado (" + std::to_string(kModFormat) + "): atualize o jogo"); continue; }
        if (*fmt < 1) { fail("formato invalido"); continue; }
        ModPackage p;
        p.id       = t["id"].value_or(std::string());
        p.name     = t["nome"].value_or(p.id);
        p.version  = t["versao"].value_or(std::string());
        p.priority = int(t["prioridade"].value_or(int64_t(0)));
        p.provisional = t["provisorio"].value_or(false);
        if (!valid_resource_id(p.id)) { fail("id invalido: '" + p.id + "'"); continue; }
        if (!valid_utf8(p.name)) { fail("nome nao e UTF-8"); continue; }
        if (const auto want = t["jogo"].value<std::string>(); want && !gameVersion.empty() && *want != gameVersion) {
            fail("feito para " + *want + ", nao para " + std::string(gameVersion));
            continue;
        }
        bool okLangs = true;
        if (const auto* arr = t["idiomas"].as_array()) {
            for (const auto& v : *arr) {
                const auto s = v.value<std::string>();
                const std::string c = s ? canonical_language(*s) : std::string();
                if (c.empty()) { fail("idioma invalido em 'idiomas': '" + (s ? *s : std::string("?")) + "'"); okLangs = false; break; }
                p.languages.push_back(c);
            }
        }
        if (!okLangs) continue;
        if (std::any_of(out.packages_.begin(), out.packages_.end(), [&](const ModPackage& q) { return q.id == p.id; })) {
            fail("id repetido: '" + p.id + "' (o outro pacote fica)");
            continue;
        }
        p.dir = fs::weakly_canonical(d, ec);
        out.packages_.push_back(std::move(p));
    }
    std::stable_sort(out.packages_.begin(), out.packages_.end(), [](const ModPackage& a, const ModPackage& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.id < b.id;
    });
    return out;
}

std::optional<ModPackages::Found> ModPackages::find(std::string_view pattern, std::string_view language) const {
    const size_t mark = pattern.find("{idioma}");
    std::vector<std::string> langs = mark == std::string_view::npos ? std::vector<std::string>{""} : language_fallback(language);
    // por idioma primeiro (o exato em qualquer pacote vence o recuo), depois por prioridade
    for (const std::string& lg : langs) {
        std::string rel(pattern);
        if (mark != std::string_view::npos) rel.replace(mark, 8, lg);
        for (const ModPackage& p : packages_) {
            const auto f = safe_join(p.dir, rel);
            std::error_code ec;
            if (f && fs::is_regular_file(*f, ec)) return Found{*f, &p, lg};
        }
    }
    return std::nullopt;
}

} // namespace bs::native::localization
