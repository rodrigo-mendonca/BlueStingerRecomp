#include "ui/i18n.h"

#include <algorithm>
#include <cwctype>

namespace bs::builder::ui {

extern const Language kPtBr;
extern const Language kEnUs;

namespace {

// Novo idioma: lang_<codigo>.cpp com a tabela e uma linha aqui.
const Language* const kLanguages[] = {&kPtBr, &kEnUs};

const Language* g_current = &kEnUs;

bool sameCode(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(x) == std::tolower(y); });
}

const wchar_t* lookup(const Language& l, std::string_view key) {
    for (const Entry& e : l.entries)
        if (key == e.key) return e.text;
    return nullptr;
}

} // namespace

std::span<const Language* const> languages() { return kLanguages; }

const Language* find_language(std::string_view code) {
    for (const Language* l : kLanguages)
        if (sameCode(l->code, code)) return l;
    return nullptr;
}

std::string initial_language(std::string_view saved, const std::vector<std::wstring>& windowsPreferred) {
    if (const Language* l = find_language(saved)) return l->code;
    if (!windowsPreferred.empty()) {
        std::string first;
        for (wchar_t c : windowsPreferred.front()) first += c < 128 ? char(c) : '?';
        if (const Language* l = find_language(first)) return l->code;
    }
    return kEnUs.code;
}

void set_language(std::string_view code) {
    const Language* l = find_language(code);
    g_current         = l ? l : &kEnUs;
}

const Language& current_language() { return *g_current; }

std::wstring tr(std::string_view key) {
    if (const wchar_t* t = lookup(*g_current, key)) return t;
    if (const wchar_t* t = lookup(kEnUs, key)) return t;
    return std::wstring(key.begin(), key.end());
}

std::wstring tr(std::string_view key, std::span<const std::wstring> args) {
    const std::wstring t = tr(key);
    std::wstring       out;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == L'{' && i + 2 < t.size() && std::iswdigit(t[i + 1]) && t[i + 2] == L'}') {
            const size_t n = size_t(t[i + 1] - L'0');
            if (n < args.size()) out += args[n];
            i += 2;
            continue;
        }
        out += t[i];
    }
    return out;
}

std::vector<int> placeholders(std::wstring_view t) {
    std::vector<int> r;
    for (size_t i = 0; i + 2 < t.size(); ++i)
        if (t[i] == L'{' && std::iswdigit(t[i + 1]) && t[i + 2] == L'}') r.push_back(t[i + 1] - L'0');
    std::sort(r.begin(), r.end());
    r.erase(std::unique(r.begin(), r.end()), r.end());
    return r;
}

} // namespace bs::builder::ui
