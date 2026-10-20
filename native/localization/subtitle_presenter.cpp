#include "localization/subtitle_presenter.h"

#include <algorithm>

namespace bs::native::localization {

std::u32string utf8_to_u32(std::string_view s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t        n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
        char32_t            cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
        for (size_t k = 1; k <= n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out += cp;
        i += n + 1;
    }
    return out;
}

SubtitleLayout layout_subtitle(std::u32string_view text, const Rect& content, const SubtitleStyle& st, std::span<const Rect> avoid,
                               ITextMeasurer& m) {
    SubtitleLayout L;
    L.px            = std::max(1.0f, content.h * st.heightFraction * st.scale);
    const float maxW = content.w * (1 - 2 * st.safeMargin);
    // quebra: paragrafos por '\n', palavras por espaco, palavra longa por caractere
    std::vector<std::u32string> lines;
    size_t                      p = 0;
    while (p <= text.size()) {
        const size_t           nl   = text.find(U'\n', p);
        const std::u32string_view para = text.substr(p, nl == std::u32string_view::npos ? std::u32string_view::npos : nl - p);
        std::u32string         cur;
        size_t                 q = 0;
        while (q < para.size()) {
            size_t sp = para.find(U' ', q);
            std::u32string word(para.substr(q, sp == std::u32string_view::npos ? std::u32string_view::npos : sp - q));
            q = sp == std::u32string_view::npos ? para.size() : sp + 1;
            const std::u32string tryLine = cur.empty() ? word : cur + U' ' + word;
            if (m.width(tryLine, L.px) <= maxW) { cur = tryLine; continue; }
            if (!cur.empty()) lines.push_back(cur), cur.clear();
            while (m.width(word, L.px) > maxW && word.size() > 1) {  // sem espacos (japones) ou palavra enorme
                size_t k = 1;
                while (k < word.size() && m.width(word.substr(0, k + 1), L.px) <= maxW) ++k;
                lines.push_back(word.substr(0, k));
                word.erase(0, k);
            }
            cur = word;
        }
        lines.push_back(cur);
        if (nl == std::u32string_view::npos) break;
        p = nl + 1;
    }
    if (int(lines.size()) > st.maxLines) {
        lines.resize(size_t(st.maxLines));
        lines.back() += U"...";
        L.truncated = true;
    }
    L.lines = std::move(lines);
    float w = 0;
    for (const auto& l : L.lines) w = std::max(w, m.width(l, L.px));
    const float h = m.line_height(L.px) * float(L.lines.size()) + 2 * st.outlinePx;
    L.box = {content.x + (content.w - w) / 2, content.y + content.h * (1 - st.safeMargin) - h, w, h};
    // sobe acima do que cobrir (HUD); varias passadas porque subir pode encostar em outro
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        for (const Rect& a : avoid)
            if (L.box.intersects(a)) L.box.y = a.y - h - st.outlinePx, moved = true;
        if (!moved) break;
    }
    L.box.y = std::max(L.box.y, content.y + content.h * st.safeMargin);  // nunca acima da area segura
    return L;
}

} // namespace bs::native::localization
