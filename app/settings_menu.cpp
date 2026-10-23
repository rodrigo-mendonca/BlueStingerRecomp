#include "settings_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>

namespace bs {
namespace st = native::settings;

namespace {

std::string onoff(bool b) { return b ? "ligado" : "desligado"; }
// o nome do idioma no menu (o codigo BCP-47 para os que nao estao aqui)
std::string language_label(const std::string& lang) {
    if (lang == "pt-BR") return "Português (Brasil)";
    if (lang == "en-US" || lang == "en") return "English";
    return lang;
}
std::string pct(uint32_t v) { return std::to_string(v) + " %"; }
std::string camera_label(st::CameraMode m) {
    static const char* n[] = {"Original Dreamcast", "C\u00e2mera Livre"};
    return n[size_t(m)];
}

template <class E> E cycle(E e, int dir, int count) { return E((int(e) + dir + count) % count); }

uint32_t step(uint32_t v, int dir, uint32_t lo, uint32_t hi, uint32_t by) {
    const int64_t n = int64_t(v) + int64_t(dir) * by;
    return uint32_t(std::clamp<int64_t>(n, lo, hi));
}

} // namespace

// Uma linha do menu: rotulo, valor, se pode mudar, e o que Esquerda/Direita (ou Confirmar) fazem
struct SettingsMenu::Entry {
    std::string                         label, value;
    bool                                enabled = true;
    std::function<void(Preferences&, int)> change;  // dir = -1 / +1 (Confirmar = +1)
    std::function<void(SettingsMenu&)>     action;  // em vez de mudar valor: abrir pagina etc.
};

SettingsMenu::SettingsMenu(Preferences prefs, st::Capabilities caps, std::vector<std::string> languages)
    : prefs_(std::move(prefs)), caps_(caps), languages_(std::move(languages)) {
    if (languages_.empty()) languages_.push_back("en-US");
}

void SettingsMenu::open() { open_ = true, page_ = Page::Main, sel_ = 0; }
void SettingsMenu::close() {
    if (pending_ == Pending::Display) prefs_ = before_, dirty_ = true;  // fechar sem confirmar: volta
    pending_ = Pending::None, open_ = false;
}

bool SettingsMenu::take_changes(Preferences& out) {
    if (!dirty_) return false;
    out = prefs_, dirty_ = false;
    return true;
}

std::vector<SettingsMenu::Entry> SettingsMenu::entries() const {
    std::vector<Entry> e;
    const st::Features& f = caps_.features;
    auto page = [](Page p) { return [p](SettingsMenu& m) { m.page_ = p, m.sel_ = 0; }; };
    switch (page_) {
    case Page::Main:
        e.push_back({"Gr\u00e1ficos", "", true, nullptr, page(Page::Graphics)});
        e.push_back({"\u00c1udio", "", true, nullptr, page(Page::Audio)});
        e.push_back({"Legendas", "", true, nullptr, page(Page::Subtitles)});
        e.push_back({"Controles", "", true, nullptr, page(Page::Controls)});
        e.push_back({"Restaurar padr\u00f5es", "", true, nullptr, [](SettingsMenu& m) {
                         m.pending_ = Pending::Restore;
                     }});
        e.push_back({"Voltar ao jogo", "", true, nullptr, [](SettingsMenu& m) { m.close(); }});
        break;
    case Page::Graphics: {
        const auto& g   = prefs_.graphics;
        const char* res[] = {"original (640x480)", "720p", "1080p", "1440p", "4K"};
        const char* asp[] = {"4:3 (original)", "16:9", "autom\u00e1tico"};
        const char* win[] = {"janela", "tela cheia", "sem bordas"};
        const char* flt[] = {"original", "bilinear", "anisotr\u00f3pica"};
        const char* pre[] = {"Original Dreamcast", "Equilibrado", "Alta Qualidade", "Personalizado"};
        auto        soon  = [](bool have, std::string v) { return have ? v : v + " (em breve)"; };
        e.push_back({"Renderizador", g.vulkan ? "Vulkan" : "software (original)", caps_.vulkan,
                     [](Preferences& p, int) { p.graphics.vulkan = !p.graphics.vulkan; }});
        const bool presets = f.scaling || f.filtering || f.msaa;  // sem melhorias, so o Original existe
        e.push_back({"Predefini\u00e7\u00e3o", soon(presets, pre[size_t(st::classify(g))]), presets, [](Preferences& p, int d) {
                         const auto cur = st::classify(p.graphics);
                         const int  n   = (int(cur == st::Preset::Custom ? st::Preset::Original : cur) + d + 3) % 3;
                         st::GraphicsSettings g = st::preset(st::Preset(n));
                         g.window = p.graphics.window, g.window_w = p.graphics.window_w, g.window_h = p.graphics.window_h;
                         g.aspect = p.graphics.aspect, g.vsync = p.graphics.vsync;
                         g.vulkan = p.graphics.vulkan;  // a predefinicao nao troca o renderizador
                         p.graphics = g;
                     }});
        // so as resolucoes que cabem no limite (caps.max_height) e so com o Vulkan (o software desenha na do jogo)
        int resolutions = 1;
        for (const uint32_t hh : {720u, 1080u, 1440u, 2160u}) resolutions += hh <= caps_.max_height;
        const bool        scaling  = f.scaling && g.vulkan;
        const std::string resValue = !f.scaling ? soon(false, res[size_t(g.resolution)])
                                     : g.vulkan ? std::string(res[size_t(g.resolution)])
                                                : std::string(res[size_t(g.resolution)]) + " (s\u00f3 com Vulkan)";
        e.push_back({"Resolu\u00e7\u00e3o interna", resValue, scaling, [resolutions](Preferences& p, int d) {
                         p.graphics.resolution = cycle(p.graphics.resolution, d, resolutions);
                     }});
        e.push_back({"Escala de renderiza\u00e7\u00e3o", soon(f.scaling, pct(g.scale_pct)), scaling,
                     [](Preferences& p, int d) { p.graphics.scale_pct = step(p.graphics.scale_pct, d, 50, 200, 25); }});
        e.push_back({"Propor\u00e7\u00e3o", soon(f.aspect_wide, asp[size_t(g.aspect)]), f.aspect_wide,
                     [](Preferences& p, int d) { p.graphics.aspect = cycle(p.graphics.aspect, d, 3); }});
        e.push_back({"Modo de tela", soon(f.window_modes, win[size_t(g.window)]), f.window_modes,
                     [](Preferences& p, int d) { p.graphics.window = cycle(p.graphics.window, d, 3); }});
        {
            static constexpr uint32_t sizes[][2] = {{0, 0}, {640, 480}, {960, 720}, {1280, 960}, {1440, 1080}, {1920, 1440}};
            const bool on = f.window_modes && g.window == st::WindowMode::Windowed;
            const std::string v = g.window_w ? std::to_string(g.window_w) + "x" + std::to_string(g.window_h) : "padr\u00e3o";
            e.push_back({"Tamanho da janela", f.window_modes ? v : v + " (em breve)", on, [](Preferences& p, int d) {
                             int i = 0;
                             for (int k = 0; k < 6; ++k)
                                 if (sizes[k][0] == p.graphics.window_w && sizes[k][1] == p.graphics.window_h) i = k;
                             i = (i + d + 6) % 6;
                             p.graphics.window_w = sizes[i][0], p.graphics.window_h = sizes[i][1];
                         }});
        }
        e.push_back({"V-Sync", soon(f.vsync, onoff(g.vsync)), f.vsync, [](Preferences& p, int) { p.graphics.vsync = !p.graphics.vsync; }});
        e.push_back({"Filtragem de texturas", soon(f.filtering, flt[size_t(g.filter)]), f.filtering,
                     [](Preferences& p, int d) { p.graphics.filter = cycle(p.graphics.filter, d, 3); }});
        e.push_back({"Antialiasing (MSAA)", soon(f.msaa, g.msaa == 1 ? "desligado" : std::to_string(g.msaa) + "x"), f.msaa,
                     [](Preferences& p, int d) {
                         const uint32_t v[] = {1, 2, 4, 8};
                         int            i   = int(std::find(v, v + 4, p.graphics.msaa) - v);
                         p.graphics.msaa    = v[std::clamp(i + d, 0, 3)];
                     }});
        e.push_back({"Sombras", soon(f.shadows, g.shadows == st::Shadows::Original ? "original" : "aprimoradas"), f.shadows,
                     [](Preferences& p, int d) { p.graphics.shadows = cycle(p.graphics.shadows, d, 2); }});
        e.push_back({"Ilumina\u00e7\u00e3o aprimorada", soon(f.lighting, onoff(g.lighting)), f.lighting,
                     [](Preferences& p, int) { p.graphics.lighting = !p.graphics.lighting; }});
        e.push_back({"Texturas HD", soon(f.hd_textures, onoff(g.hd_textures)), f.hd_textures,
                     [](Preferences& p, int) { p.graphics.hd_textures = !p.graphics.hd_textures; }});
        {  // N7c (secao 21): os efeitos de imagem numa pagina propria
            const bool any = f.color || f.tonemap || f.sharpen || f.bloom || f.crt || f.post || f.shaders;
            const bool on  = g.color || !g.shader.empty() || g.bloom || g.tonemap || g.sharpen != 0 || g.crt;
            e.push_back({"Efeitos de imagem", any ? (on ? "ligados" : "desligados") : "(em breve)", any, nullptr, page(Page::Effects)});
        }
        e.push_back({"Voltar", "", true, nullptr, page(Page::Main)});
        break;
    }
    case Page::Effects: {
        // so a imagem do jogo; menu e legendas ficam por cima, sem efeito. Original Dreamcast = tudo desligado.
        const auto& g = prefs_.graphics;
        auto        soon = [](bool have, std::string v) { return have ? v : v + " (em breve)"; };
        auto        num  = [](float v, int places) {
            char b[24];
            std::snprintf(b, sizeof b, "%.*f", places, double(v));
            return std::string(b);
        };
        auto fstep = [](float* (*field)(Preferences&), float by, float lo, float hi) {
            return [=](Preferences& p, int d) {
                float& v = *field(p);
                v        = std::clamp(std::round((v + by * float(d)) / by) * by, lo, hi);
            };
        };
        e.push_back({"Corre\u00e7\u00e3o de cor", soon(f.color, onoff(g.color)), f.color,
                     [](Preferences& p, int) { p.graphics.color = !p.graphics.color; }});
        const bool cc = f.color && g.color;
        e.push_back({"Brilho", num(g.brightness, 2), cc, fstep([](Preferences& p) { return &p.graphics.brightness; }, 0.05f, -0.5f, 0.5f)});
        e.push_back({"Contraste", num(g.contrast, 2), cc, fstep([](Preferences& p) { return &p.graphics.contrast; }, 0.05f, 0.5f, 1.5f)});
        e.push_back({"Gamma", num(g.gamma, 2), cc, fstep([](Preferences& p) { return &p.graphics.gamma; }, 0.05f, 0.5f, 2.5f)});
        e.push_back({"Satura\u00e7\u00e3o", num(g.saturation, 2), cc, fstep([](Preferences& p) { return &p.graphics.saturation; }, 0.05f, 0.f, 2.f)});
        e.push_back({"Tonemapping", soon(f.tonemap, onoff(g.tonemap)), f.tonemap,
                     [](Preferences& p, int) { p.graphics.tonemap = !p.graphics.tonemap; }});
        const bool tm = f.tonemap && g.tonemap;
        e.push_back({"  Exposi\u00e7\u00e3o (EV)", num(g.exposure, 2), tm, fstep([](Preferences& p) { return &p.graphics.exposure; }, 0.1f, -2.f, 2.f)});
        e.push_back({"  Altas luzes", num(g.highlights, 2), tm, fstep([](Preferences& p) { return &p.graphics.highlights; }, 0.1f, 0.f, 1.f)});
        e.push_back({"  Sombras", num(g.tone_shadows, 2), tm, fstep([](Preferences& p) { return &p.graphics.tone_shadows; }, 0.05f, -0.5f, 0.5f)});
        e.push_back({"Nitidez", soon(f.sharpen, g.sharpen == 0 ? "desligada" : pct(uint32_t(std::lround(g.sharpen * 100)))), f.sharpen,
                     fstep([](Preferences& p) { return &p.graphics.sharpen; }, 0.1f, 0.f, 1.f)});
        e.push_back({"Bloom", soon(f.bloom, onoff(g.bloom)), f.bloom, [](Preferences& p, int) { p.graphics.bloom = !p.graphics.bloom; }});
        const bool bl = f.bloom && g.bloom;
        e.push_back({"  Limiar", num(g.bloom_threshold, 2), bl, fstep([](Preferences& p) { return &p.graphics.bloom_threshold; }, 0.05f, 0.f, 1.f)});
        e.push_back({"  Raio", num(g.bloom_radius * 100, 1) + " %", bl,
                     fstep([](Preferences& p) { return &p.graphics.bloom_radius; }, 0.005f, 0.005f, 0.05f)});
        e.push_back({"  Intensidade", num(g.bloom_strength, 2), bl, fstep([](Preferences& p) { return &p.graphics.bloom_strength; }, 0.1f, 0.f, 2.f)});
        e.push_back({"SSAO", soon(false, onoff(g.ssao)), false, nullptr});
        {
            const bool tall = st::internal_height(g) >= st::kCrtMinHeight;  // as linhas do jogo precisam de espaco
            e.push_back({"Filtro CRT", !f.crt ? soon(false, onoff(g.crt)) : tall ? onoff(g.crt) : "requer resolu\u00e7\u00e3o interna 1080p ou mais",
                         f.crt && tall, [](Preferences& p, int) { p.graphics.crt = !p.graphics.crt; }});
            const bool cr = f.crt && tall && g.crt;
            e.push_back({"  Linhas de varredura", num(g.crt_scanlines, 2), cr,
                         fstep([](Preferences& p) { return &p.graphics.crt_scanlines; }, 0.1f, 0.f, 1.f)});
            e.push_back({"  M\u00e1scara", num(g.crt_mask, 2), cr, fstep([](Preferences& p) { return &p.graphics.crt_mask; }, 0.1f, 0.f, 1.f)});
            e.push_back({"  Vinheta", num(g.crt_vignette, 2), cr, fstep([](Preferences& p) { return &p.graphics.crt_vignette; }, 0.1f, 0.f, 1.f)});
        }
        // shader de mod: "nenhum" ou um dos validos (os invalidos aparecem numa linha so, com o motivo no log)
        std::vector<const ShaderChoice*> valid;
        int                              invalid = 0;
        for (const auto& s : shaders_) s.error.empty() ? valid.push_back(&s) : void(++invalid);
        e.push_back({"Shader de mod", soon(f.shaders, g.shader.empty() ? "nenhum" : g.shader), f.shaders && !valid.empty(),
                     [valid](Preferences& p, int d) {
                         int i = -1;  // -1 = nenhum
                         for (int k = 0; k < int(valid.size()); ++k)
                             if (valid[size_t(k)]->name == p.graphics.shader) i = k;
                         const int n = int(valid.size()) + 1;
                         i           = ((i + 1 + d) % n + n) % n - 1;
                         p.graphics.shader = i < 0 ? std::string() : valid[size_t(i)]->name;
                     }});
        for (const ShaderChoice* s : valid)
            if (s->name == g.shader)
                for (const auto& q : s->params) {
                    const auto  it = g.shader_params.find(q.name);
                    const float v  = it != g.shader_params.end() ? it->second : q.def;
                    e.push_back({"  " + q.label, num(v, 2), f.shaders, [q, v](Preferences& p, int d) {
                                     p.graphics.shader_params[q.name] = std::clamp(v + q.step * float(d), q.min, q.max);
                                 }});
                }
        if (invalid) e.push_back({"Shaders inv\u00e1lidos", std::to_string(invalid) + " (desativados; ver o log)", false, nullptr});
        e.push_back({"Voltar", "", true, nullptr, page(Page::Graphics)});
        break;
    }
    case Page::Audio: {
        const auto& a = prefs_.audio;
        auto        vol = [](uint32_t st::AudioSettings::*m) {
            return [m](Preferences& p, int d) { p.audio.*m = step(p.audio.*m, d, 0, 100, 5); };
        };
        e.push_back({"Volume geral", pct(a.master), true, vol(&st::AudioSettings::master)});
        e.push_back({"M\u00fasica", pct(a.music), true, vol(&st::AudioSettings::music)});
        e.push_back({"Efeitos", pct(a.effects), true, vol(&st::AudioSettings::effects)});
        e.push_back({"Vozes", pct(a.voices), true, vol(&st::AudioSettings::voices)});
        e.push_back({"Voltar", "", true, nullptr, page(Page::Main)});
        break;
    }
    case Page::Subtitles: {
        const auto& s     = prefs_.subtitles;
        const auto  langs = languages_;
        // os idiomas dos pacotes instalados e "Desativadas" no fim da volta
        e.push_back({"Idioma", s.enabled ? language_label(s.language) : "Desativadas", true, [langs](Preferences& p, int d) {
                         const int n  = int(langs.size()) + 1;
                         auto      it = std::find(langs.begin(), langs.end(), p.subtitles.language);
                         const int i  = !p.subtitles.enabled ? n - 1 : it == langs.end() ? 0 : int(it - langs.begin());
                         const int j  = ((i + d) % n + n) % n;
                         p.subtitles.enabled = j < n - 1;
                         if (p.subtitles.enabled) p.subtitles.language = langs[size_t(j)];
                     }});
        e.push_back({"Tamanho do texto", pct(s.size_pct), s.enabled,
                     [](Preferences& p, int d) { p.subtitles.size_pct = step(p.subtitles.size_pct, d, 50, 200, 10); }});
        e.push_back({"Contorno", onoff(s.outline), s.enabled, [](Preferences& p, int) {
                         p.subtitles.outline = !p.subtitles.outline;
                         if (!p.subtitles.outline && !p.subtitles.box) p.subtitles.box = true;  // um dos dois sempre
                     }});
        e.push_back({"Fundo", onoff(s.box), s.enabled, [](Preferences& p, int) {
                         p.subtitles.box = !p.subtitles.box;
                         if (!p.subtitles.outline && !p.subtitles.box) p.subtitles.outline = true;
                     }});
        e.push_back({"Voltar", "", true, nullptr, page(Page::Main)});
        break;
    }
    case Page::Controls:
        e.push_back({"C\u00e2mera", camera_label(prefs_.camera.mode), true, nullptr, page(Page::Camera)});
        for (const MenuItem& i : controlsInfo_) e.push_back({i.label, i.value, true, nullptr, nullptr});
        e.push_back({"Remapear pelo menu", "em breve (controles.toml)", false, nullptr, nullptr});
        e.push_back({"Voltar", "", true, nullptr, page(Page::Main)});
        break;
    case Page::Camera: {
        // N7c (secao 23): analogico direito. Original = a camera do jogo (nada instalado): as outras opcoes ficam inativas.
        const auto& c   = prefs_.camera;
        const bool  on  = c.mode != st::CameraMode::Original;
        auto        num = [](float v, int places, const char* unit) {
            char b[32];
            std::snprintf(b, sizeof b, "%.*f%s", places, double(v), unit);
            return std::string(b);
        };
        auto fstep = [](float st::CameraSettings::*m, float by, float lo, float hi) {
            return [=](Preferences& p, int d) { p.camera.*m = std::clamp(std::round((p.camera.*m + by * float(d)) / by) * by, lo, hi); };
        };
        e.push_back({"Modo", camera_label(c.mode), true, [](Preferences& p, int d) { p.camera.mode = cycle(p.camera.mode, d, 2); }});
        // no Original nao ha deslocamento: o movimento e sempre o do console
        e.push_back({"Movimentação", !on || c.movement == st::MoveMode::Original ? "Original" : "Relativa à câmera", on,
                     [](Preferences& p, int) {
                         p.camera.movement = p.camera.movement == st::MoveMode::Camera ? st::MoveMode::Original : st::MoveMode::Camera;
                     }});
        e.push_back({"Sensibilidade horizontal", num(c.sens_x, 0, " \u00b0/s"), on, fstep(&st::CameraSettings::sens_x, 10, 10, 360)});
        e.push_back({"Sensibilidade vertical", num(c.sens_y, 0, " \u00b0/s"), on, fstep(&st::CameraSettings::sens_y, 10, 10, 360)});
        e.push_back({"Zona morta", num(c.deadzone, 2, ""), on, fstep(&st::CameraSettings::deadzone, 0.05f, 0, 0.6f)});
        e.push_back({"Inverter eixo Y", onoff(c.invert_y), on, [](Preferences& p, int) { p.camera.invert_y = !p.camera.invert_y; }});
        e.push_back({"Suaviza\u00e7\u00e3o", num(c.smoothing, 1, ""), on, fstep(&st::CameraSettings::smoothing, 0.1f, 0, 0.9f)});
        e.push_back({"Recentralizar com R3", onoff(c.r3_recenter), on, [](Preferences& p, int) { p.camera.r3_recenter = !p.camera.r3_recenter; }});
        e.push_back({"Limite para cima", num(c.pitch_up, 0, "\u00b0"), on, fstep(&st::CameraSettings::pitch_up, 5, 0, 45)});
        e.push_back({"Limite para baixo", num(c.pitch_down, 0, "\u00b0"), on, fstep(&st::CameraSettings::pitch_down, 5, 0, 45)});
        e.push_back({"Voltar", "", true, nullptr, page(Page::Controls)});
        break;
    }
    }
    return e;
}

void SettingsMenu::commit(const Preferences& wanted, double now) {
    Preferences next = wanted;
    next.graphics    = st::clamp_to(next.graphics, caps_);  // nada que o renderizador nao tenha
    if (next == prefs_) return;
    if (st::requires_confirmation(prefs_.graphics, next.graphics) && pending_ != Pending::Display)
        before_ = prefs_, pending_ = Pending::Display, deadline_ = now + st::kConfirmSeconds;
    prefs_ = next, dirty_ = true;
}

void SettingsMenu::change(int dir, double now) {
    const auto e = entries();
    if (sel_ < 0 || sel_ >= int(e.size()) || !e[size_t(sel_)].enabled) return;
    if (e[size_t(sel_)].change) {
        Preferences next = prefs_;
        e[size_t(sel_)].change(next, dir);
        commit(next, now);
    }
}

void SettingsMenu::input(MenuAction a, double now) {
    if (!open_) return;
    tick(now);
    if (pending_ != Pending::None) {  // so Confirmar (mantem / restaura) ou Voltar (desfaz / cancela)
        if (a == MenuAction::Confirm) {
            if (pending_ == Pending::Restore) {
                pending_ = Pending::None;
                commit(Preferences{}, now);  // Original Dreamcast, volumes 100, legendas en-US ligadas
                if (pending_ == Pending::Display) pending_ = Pending::None;  // restaurar ja foi confirmado
            } else {
                pending_ = Pending::None;  // mantem
            }
        } else if (a == MenuAction::Back) {
            if (pending_ == Pending::Display) prefs_ = before_, dirty_ = true;
            pending_ = Pending::None;
        }
        return;
    }
    const auto e = entries();
    const int  n = int(e.size());
    switch (a) {
    case MenuAction::Up: sel_ = (sel_ - 1 + n) % n; break;
    case MenuAction::Down: sel_ = (sel_ + 1) % n; break;
    case MenuAction::Left: change(-1, now); break;
    case MenuAction::Right: change(+1, now); break;
    case MenuAction::Confirm:
        if (e[size_t(sel_)].action) e[size_t(sel_)].action(*this);
        else change(+1, now);
        break;
    case MenuAction::Back:
        if (page_ == Page::Main) close();
        else page_ = page_ == Page::Effects ? Page::Graphics : page_ == Page::Camera ? Page::Controls : Page::Main, sel_ = 0;
        break;
    }
}

void SettingsMenu::mark_shader_invalid(const std::string& name, const std::string& why) {
    for (auto& s : shaders_)
        if (s.name == name) s.error = why;
    if (prefs_.graphics.shader == name) prefs_.graphics.shader.clear(), dirty_ = true;  // desativado
}

void SettingsMenu::cancel_display() {
    if (pending_ != Pending::Display) return;
    prefs_ = before_, dirty_ = true, pending_ = Pending::None;
}

void SettingsMenu::select(int index) {
    if (!open_ || pending_ != Pending::None) return;
    const int n = int(entries().size());
    if (index >= 0 && index < n) sel_ = index;
}

void SettingsMenu::adjust(int index, int dir, double now) {
    now_ = now;
    if (!open_ || pending_ != Pending::None) return;
    select(index);
    change(dir < 0 ? -1 : +1, now);
}

void SettingsMenu::tick(double now) {
    now_ = now;
    if (pending_ == Pending::Display && now >= deadline_) prefs_ = before_, dirty_ = true, pending_ = Pending::None;
}

MenuView SettingsMenu::view() const {
    MenuView    v;
    const char* titles[] = {"Configura\u00e7\u00f5es", "Configura\u00e7\u00f5es > Gr\u00e1ficos", "Configura\u00e7\u00f5es > \u00c1udio", "Configura\u00e7\u00f5es > Legendas",
                            "Configura\u00e7\u00f5es > Controles", "Configura\u00e7\u00f5es > Gr\u00e1ficos > Efeitos de imagem",
                            "Configura\u00e7\u00f5es > Controles > C\u00e2mera"};
    v.title = titles[size_t(page_)];
    for (const auto& e : entries()) v.items.push_back({e.label, e.value, e.enabled});
    v.selected = sel_;
    if (pending_ == Pending::Display) {
        const int left = std::max(0, int(std::ceil(deadline_ - now_)));
        v.prompt = "Manter esta configura\u00e7\u00e3o de tela? Volta sozinha em " + std::to_string(left) +
                   " s. (Enter/A = manter, Esc/B = desfazer)";
    }
    if (pending_ == Pending::Restore) v.prompt = "Restaurar todas as configura\u00e7\u00f5es padr\u00e3o? (Enter/A = sim, Esc/B = n\u00e3o)";
    return v;
}

} // namespace bs
