#include "video/subtitles.h"

#include "core/com_scope.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bs::native::video {

namespace {

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int    n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// "00:01:02,345" (SRT) ou "01:02.345" / "00:01:02.345" (VTT) em 100 ns; -1 se invalido
int64_t parseTime(std::string_view t) {
    int    parts[4] = {0, 0, 0, 0}, n = 0, digits = 0;
    size_t i = 0;
    for (; i < t.size() && n < 4; ++i) {
        const char c = t[i];
        if (c >= '0' && c <= '9') parts[n] = parts[n] * 10 + (c - '0'), ++digits;
        else if (c == ':' || c == ',' || c == '.') {
            if (!digits) return -1;
            ++n, digits = 0;
        } else break;
    }
    if (digits) ++n;
    int64_t h = 0, m = 0, s = 0, ms = 0;
    if (n == 4) h = parts[0], m = parts[1], s = parts[2], ms = parts[3];
    else if (n == 3) m = parts[0], s = parts[1], ms = parts[2];
    else return -1;
    if (m > 59 || s > 59 || ms > 999) return -1;
    return ((h * 3600 + m * 60 + s) * 1000 + ms) * 10'000;
}

std::string stripTags(std::string s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') {  // <i>, </i>, <b>, <font ...>, <c.classe>
            const size_t e = s.find('>', i);
            if (e != std::string::npos) { i = e; continue; }
        }
        if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '\\') {  // {\an8} do SRT
            const size_t e = s.find('}', i);
            if (e != std::string::npos) { i = e; continue; }
        }
        out += s[i];
    }
    return out;
}

} // namespace

bool parse_subtitles(std::string_view in, std::vector<SubtitleCue>& cues, std::vector<std::string>* warnings) {
    cues.clear();
    std::string text(in);
    if (text.size() >= 3 && uint8_t(text[0]) == 0xEF && uint8_t(text[1]) == 0xBB && uint8_t(text[2]) == 0xBF) text.erase(0, 3);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    std::istringstream ss(text);
    std::string        line;
    int                lineNo = 0;
    while (std::getline(ss, line)) {
        ++lineNo;
        const size_t arrow = line.find("-->");
        if (arrow == std::string::npos) continue;  // indices do SRT, WEBVTT, NOTE, linhas soltas
        std::string a = line.substr(0, arrow), b = line.substr(arrow + 3);
        auto trim = [](std::string& s) {
            const size_t p = s.find_first_not_of(" \t");
            s = p == std::string::npos ? std::string() : s.substr(p);
        };
        trim(a), trim(b);
        SubtitleCue c;
        c.start100ns = parseTime(a);
        c.end100ns   = parseTime(b);  // o VTT pode ter configuracoes depois do tempo: parseTime para nelas
        std::string body;
        while (std::getline(ss, line) && !line.empty()) {
            ++lineNo;
            if (!body.empty()) body += '\n';
            body += stripTags(line);
        }
        ++lineNo;
        if (c.start100ns < 0 || c.end100ns < 0 || c.end100ns <= c.start100ns) {
            if (warnings) warnings->push_back("legenda: tempo invalido perto da linha " + std::to_string(lineNo));
            continue;
        }
        c.text = widen(body);
        cues.push_back(std::move(c));
    }
    std::stable_sort(cues.begin(), cues.end(), [](const SubtitleCue& x, const SubtitleCue& y) { return x.start100ns < y.start100ns; });
    return true;
}

bool load_subtitles(const std::filesystem::path& file, std::vector<SubtitleCue>& cues, std::string* err, std::vector<std::string>* warnings) {
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        if (err) *err = "legenda nao encontrada: " + file.filename().string();
        return false;
    }
    std::string s((std::istreambuf_iterator<char>(f)), {});
    return parse_subtitles(s, cues, warnings);
}

std::wstring subtitle_at(const std::vector<SubtitleCue>& cues, int64_t t) {
    std::wstring out;
    for (const auto& c : cues) {
        if (c.start100ns > t) break;
        if (t < c.end100ns) out += (out.empty() ? L"" : L"\n") + c.text;
    }
    return out;
}

// ---------------------------------------------------------------------------- desenho

struct SubtitleRenderer::Impl {
    ComScope             com;  // o WIC exige COM na thread do desenho
    IDWriteFactory*      dw  = nullptr;
    ID2D1Factory*        d2d = nullptr;
    IWICImagingFactory*  wic = nullptr;
    ~Impl() {
        if (wic) wic->Release();
        if (d2d) d2d->Release();
        if (dw) dw->Release();
    }
};

SubtitleRenderer::SubtitleRenderer() : impl_(std::make_unique<Impl>()) {
    // falhou alguma: ok() == false (exige as tres) e o destrutor do Impl libera as que foram criadas
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&impl_->dw));
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &impl_->d2d);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->wic));
}
SubtitleRenderer::~SubtitleRenderer() = default;
bool SubtitleRenderer::ok() const { return impl_ && impl_->dw && impl_->d2d && impl_->wic; }

void SubtitleRenderer::draw(std::span<uint32_t> argb, uint32_t w, uint32_t h, const std::wstring& text) {
    if (!ok() || text.empty() || argb.size() < size_t(w) * h) return;
    // desenha sobre uma copia do quadro (bitmap WIC PBGRA) e copia de volta
    IWICBitmap* bmp = nullptr;
    if (FAILED(impl_->wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, w * 4, UINT(w * h * 4),
                                                   reinterpret_cast<BYTE*>(argb.data()), &bmp)))
        return;
    ID2D1RenderTarget* rt = nullptr;
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    IDWriteTextFormat* fmt = nullptr;
    ID2D1SolidColorBrush *white = nullptr, *dark = nullptr;
    const float size = std::max(10.0f, float(h) * 0.065f);  // ~16 px em 240 linhas
    if (SUCCEEDED(impl_->d2d->CreateWicBitmapRenderTarget(bmp, props, &rt)) &&
        SUCCEEDED(impl_->dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, size, L"", &fmt)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &white)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.85f), &dark))) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
        const float     margin = float(h) * 0.05f;
        const D2D1_RECT_F box  = D2D1::RectF(margin, margin, float(w) - margin, float(h) - margin);
        rt->BeginDraw();
        // contorno: o texto escuro deslocado em 8 direcoes, depois o branco por cima
        const float o = std::max(1.0f, size / 14.0f);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy)
                    rt->DrawText(text.c_str(), UINT32(text.size()), fmt,
                                 D2D1::RectF(box.left + dx * o, box.top + dy * o, box.right + dx * o, box.bottom + dy * o), dark);
        rt->DrawText(text.c_str(), UINT32(text.size()), fmt, box, white);
        rt->EndDraw();
        // de volta ao quadro (o bitmap foi criado por copia)
        WICRect r{0, 0, INT(w), INT(h)};
        bmp->CopyPixels(&r, w * 4, UINT(w * h * 4), reinterpret_cast<BYTE*>(argb.data()));
        for (auto& p : argb) p |= 0xFF000000u;
    }
    if (dark) dark->Release();
    if (white) white->Release();
    if (fmt) fmt->Release();
    if (rt) rt->Release();
    bmp->Release();
}

} // namespace bs::native::video
