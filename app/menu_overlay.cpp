#include "menu_overlay.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef DrawState
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>

namespace bs {
namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int    n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

template <class T> void release(T*& p) {
    if (p) p->Release(), p = nullptr;
}

struct Rect {
    float l = 0, t = 0, r = 0, b = 0;
    bool  has(float x, float y) const { return x >= l && x < r && y >= t && y < b; }
};

// uma chave do conteudo (refazer so quando muda)
std::string key(const MenuView& v, uint32_t w, uint32_t h, float cx, float cy, float cw, float ch) {
    std::string k = v.title + "\x1f" + v.prompt + "\x1f" + std::to_string(v.selected) + "\x1f" + std::to_string(w) + "x" +
                    std::to_string(h) + "@" + std::to_string(cx) + "," + std::to_string(cy) + "," + std::to_string(cw) + "," +
                    std::to_string(ch);
    for (const auto& i : v.items) k += "\x1e" + i.label + "\x1f" + i.value + (i.enabled ? "1" : "0");
    return k;
}

} // namespace

struct MenuOverlay::Impl {
    bool                com = false;
    IDWriteFactory*     dw  = nullptr;
    ID2D1Factory*       d2d = nullptr;
    IWICImagingFactory* wic = nullptr;
    std::vector<uint32_t> buf;
    std::string           last;
    struct Row {
        int  item;
        Rect row, dec, inc;
    };
    std::vector<Row> rows;
    ~Impl() {
        release(wic), release(d2d), release(dw);
        if (com) CoUninitialize();
    }
};

MenuOverlay::MenuOverlay() : impl_(std::make_unique<Impl>()) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    impl_->com       = SUCCEEDED(hr);  // RPC_E_CHANGED_MODE: a thread ja tem COM (serve)
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&impl_->dw));
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &impl_->d2d);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->wic));
}
MenuOverlay::~MenuOverlay() = default;
bool MenuOverlay::ok() const { return impl_->dw && impl_->d2d && impl_->wic; }

const std::vector<uint32_t>& MenuOverlay::layer(const MenuView& v, uint32_t w, uint32_t h, float cx, float cy, float cw,
                                                float ch) {
    Impl& m = *impl_;
    const std::string k = key(v, w, h, cx, cy, cw, ch);
    if (k == m.last && m.buf.size() == size_t(w) * h) return m.buf;
    m.last = k;
    m.buf.assign(size_t(w) * h, 0u);
    m.rows.clear();
    if (!ok() || !w || !h) return m.buf;

    IWICBitmap*            bmp = nullptr;
    ID2D1RenderTarget*     rt  = nullptr;
    IDWriteTextFormat *    fTitle = nullptr, *fItem = nullptr, *fSmall = nullptr;
    ID2D1SolidColorBrush * bPanel = nullptr, *bSel = nullptr, *bText = nullptr, *bDim = nullptr, *bAccent = nullptr,
                         *bBox = nullptr;
    const float size = std::max(12.0f, ch * 0.032f);
    auto fmt = [&](float px, DWRITE_FONT_WEIGHT wt, IDWriteTextFormat** out) {
        return SUCCEEDED(m.dw->CreateTextFormat(L"Segoe UI", nullptr, wt, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px,
                                                L"", out));
    };
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (SUCCEEDED(m.wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bmp)) &&
        SUCCEEDED(m.d2d->CreateWicBitmapRenderTarget(bmp, props, &rt)) && fmt(size * 1.35f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &fTitle) &&
        fmt(size, DWRITE_FONT_WEIGHT_NORMAL, &fItem) && fmt(size * 0.78f, DWRITE_FONT_WEIGHT_NORMAL, &fSmall) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0.02f, 0.04f, 0.08f, 0.86f), &bPanel)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.42f, 0.78f, 0.85f), &bSel)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &bText)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.58f, 0.62f, 1), &bDim)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.80f, 1.0f, 1), &bAccent)) &&
        SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.12f, 0.18f, 0.97f), &bBox))) {
        fItem->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        // o jogo continua visivel por tras, escurecido
        ID2D1SolidColorBrush* dim = nullptr;
        if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.45f), &dim))) {
            rt->FillRectangle(D2D1::RectF(cx, cy, cx + cw, cy + ch), dim);
            release(dim);
        }
        const float pw = std::min(cw * 0.9f, ch * 1.15f), ph = ch * 0.88f;
        const Rect  panel{cx + (cw - pw) / 2, cy + (ch - ph) / 2, cx + (cw + pw) / 2, cy + (ch + ph) / 2};
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(panel.l, panel.t, panel.r, panel.b), size * 0.4f, size * 0.4f), bPanel);
        const float pad = size;
        auto text = [&](const std::wstring& s, IDWriteTextFormat* f, const Rect& r, ID2D1Brush* b) {
            rt->DrawText(s.c_str(), UINT32(s.size()), f, D2D1::RectF(r.l, r.t, r.r, r.b), b);
        };
        text(wide(v.title), fTitle, {panel.l + pad, panel.t + pad * 0.5f, panel.r - pad, panel.t + pad * 0.5f + size * 2.0f}, bText);
        const float listTop = panel.t + pad * 0.5f + size * 2.4f, footerH = size * 1.6f;
        const float listBot = panel.b - footerH - pad * 0.5f, rowH = size * 1.7f;
        const int   fit     = std::max(1, int((listBot - listTop) / rowH));
        const int   n       = int(v.items.size());
        int         first   = std::clamp(v.selected - fit / 2, 0, std::max(0, n - fit));
        for (int i = first; i < std::min(n, first + fit); ++i) {
            const MenuItem& it = v.items[size_t(i)];
            const float     y  = listTop + float(i - first) * rowH;
            const Rect      row{panel.l + pad * 0.5f, y, panel.r - pad * 0.5f, y + rowH};
            if (i == v.selected)
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(row.l, row.t + 2, row.r, row.b - 2), size * 0.3f, size * 0.3f), bSel);
            ID2D1Brush* tb = it.enabled ? static_cast<ID2D1Brush*>(bText) : bDim;
            text(wide(it.label), fItem, {row.l + pad * 0.5f, row.t, row.l + (row.r - row.l) * 0.5f, row.b}, tb);
            Impl::Row hr{i, row, {}, {}};
            if (!it.value.empty()) {
                const float vl = row.l + (row.r - row.l) * 0.5f, vr = row.r - pad * 0.5f, aw = size * 1.2f;
                const bool  arrows = it.enabled;
                if (arrows) {
                    hr.dec = {vl, row.t, vl + aw, row.b}, hr.inc = {vr - aw, row.t, vr, row.b};
                    text(L"◀", fItem, hr.dec, bAccent);
                    text(L"▶", fItem, hr.inc, bAccent);
                }
                IDWriteTextLayout* lay = nullptr;
                const std::wstring val = wide(it.value);
                if (SUCCEEDED(m.dw->CreateTextLayout(val.c_str(), UINT32(val.size()), fItem, vr - vl - 2 * aw, rowH, &lay))) {
                    lay->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    lay->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    rt->DrawTextLayout(D2D1::Point2F(vl + aw, row.t), lay, tb);
                    release(lay);
                }
            }
            m.rows.push_back(hr);
        }
        if (first > 0) text(L"▲", fSmall, {panel.l, listTop - size * 0.9f, panel.r, listTop}, bDim);
        if (first + fit < n) text(L"▼", fSmall, {panel.l, listBot, panel.r, listBot + size * 0.9f}, bDim);
        text(L"Setas / D-pad: navegar     ◀ ▶: alterar     Enter / A: escolher     Esc / B: voltar", fSmall,
             {panel.l, panel.b - footerH, panel.r, panel.b - pad * 0.2f}, bDim);
        if (!v.prompt.empty()) {  // confirmacao por cima de tudo
            const float bw = pw * 0.86f, bh = size * 4.2f;
            const Rect  box{cx + (cw - bw) / 2, cy + (ch - bh) / 2, cx + (cw + bw) / 2, cy + (ch + bh) / 2};
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(box.l, box.t, box.r, box.b), size * 0.4f, size * 0.4f), bBox);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(box.l, box.t, box.r, box.b), size * 0.4f, size * 0.4f), bAccent, 2);
            IDWriteTextLayout* lay = nullptr;
            const std::wstring p   = wide(v.prompt);
            if (SUCCEEDED(m.dw->CreateTextLayout(p.c_str(), UINT32(p.size()), fItem, box.r - box.l - 2 * pad, box.b - box.t, &lay))) {
                lay->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                lay->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(box.l + pad, box.t), lay, bText);
                release(lay);
            }
            m.rows.clear();  // com a pergunta aberta, o mouse so confirma (clique) ou volta (botao direito)
        }
        rt->EndDraw();
        WICRect r{0, 0, INT(w), INT(h)};
        bmp->CopyPixels(&r, w * 4, UINT(size_t(w) * h * 4), reinterpret_cast<BYTE*>(m.buf.data()));
    }
    release(bBox), release(bAccent), release(bDim), release(bText), release(bSel), release(bPanel);
    release(fSmall), release(fItem), release(fTitle), release(rt), release(bmp);
    return m.buf;
}

std::optional<MenuHit> MenuOverlay::hit(float x, float y) const {
    for (const auto& r : impl_->rows) {
        if (r.dec.has(x, y)) return MenuHit{r.item, MenuHit::Part::Decrease};
        if (r.inc.has(x, y)) return MenuHit{r.item, MenuHit::Part::Increase};
        if (r.row.has(x, y)) return MenuHit{r.item, MenuHit::Part::Row};
    }
    return std::nullopt;
}

} // namespace bs
