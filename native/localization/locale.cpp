#include "localization/locale.h"

#include <cctype>
#include <cstdint>

namespace bs::native::localization {

namespace {
std::vector<std::string_view> split(std::string_view s) {
    std::vector<std::string_view> out;
    size_t                        i = 0;
    while (true) {
        const size_t j = s.find('-', i);
        out.push_back(s.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i));
        if (j == std::string_view::npos) break;
        i = j + 1;
    }
    return out;
}
bool all(std::string_view s, int (*pred)(int)) {
    for (unsigned char c : s)
        if (!pred(c)) return false;
    return !s.empty();
}
bool alpha(std::string_view s) { return all(s, [](int c) { return std::isalpha(c) ? 1 : 0; }); }
bool digit(std::string_view s) { return all(s, [](int c) { return std::isdigit(c) ? 1 : 0; }); }
} // namespace

bool valid_language(std::string_view tag) {
    if (tag.empty() || tag.size() > 35) return false;
    const auto p = split(tag);
    if (!alpha(p[0]) || p[0].size() < 2 || p[0].size() > 3) return false;
    size_t i = 1;
    if (i < p.size() && p[i].size() == 4 && alpha(p[i])) ++i;                       // escrita
    if (i < p.size() && ((p[i].size() == 2 && alpha(p[i])) || (p[i].size() == 3 && digit(p[i])))) ++i;  // regiao
    return i == p.size();
}

std::string canonical_language(std::string_view tag) {
    if (!valid_language(tag)) return {};
    std::string out;
    for (std::string_view part : split(tag)) {
        if (!out.empty()) out += '-';
        std::string x(part);
        const bool  first = out.empty();
        for (size_t k = 0; k < x.size(); ++k) {
            const unsigned char c = static_cast<unsigned char>(x[k]);
            if (first) x[k] = char(std::tolower(c));
            else if (part.size() == 4) x[k] = char(k == 0 ? std::toupper(c) : std::tolower(c));
            else x[k] = char(std::toupper(c));
        }
        out += x;
    }
    return out;
}

std::vector<std::string> language_fallback(std::string_view tag) {
    std::vector<std::string> out;
    std::string              c = canonical_language(tag);
    while (!c.empty()) {
        out.push_back(c);
        const size_t dash = c.rfind('-');
        if (dash == std::string::npos) break;
        c.resize(dash);
    }
    return out;
}

bool valid_resource_id(std::string_view id) {
    if (id.empty() || id.size() > 128 || id.front() == '/' || id.back() == '/') return false;
    if (id.find("..") != std::string_view::npos || id.find("//") != std::string_view::npos) return false;
    for (unsigned char c : id)
        if (!(std::islower(c) || std::isdigit(c) || c == '.' || c == '_' || c == '-' || c == ':' || c == '/')) return false;
    return true;
}

bool valid_utf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t              n;
        uint32_t            cp;
        if (c < 0x80) { ++i; continue; }
        if ((c & 0xE0) == 0xC0) n = 1, cp = c & 0x1F;
        else if ((c & 0xF0) == 0xE0) n = 2, cp = c & 0x0F;
        else if ((c & 0xF8) == 0xF0) n = 3, cp = c & 0x07;
        else return false;
        if (i + n >= s.size()) return false;  // faltam bytes de continuacao
        for (size_t k = 1; k <= n; ++k) {
            const unsigned char d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += n + 1;
    }
    return true;
}

} // namespace bs::native::localization
