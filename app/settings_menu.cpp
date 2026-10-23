#include "settings_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>

namespace bs {
namespace st = native::settings;

namespace {

// o nome do idioma no menu, sempre no proprio idioma (o codigo BCP-47 para os que nao estao aqui)
std::string language_label(const std::string& lang) {
    if (lang == "pt-BR") return "Português (Brasil)";
    if (lang == "en-US" || lang == "en") return "English";
    return lang;
}
std::string pct(uint32_t v) { return std::to_string(v) + " %"; }

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
    bool                                submenu = false;
};

SettingsMenu::SettingsMenu(Preferences prefs, st::Capabilities caps, std::vector<std::string> languages)
    : prefs_(std::move(prefs)), caps_(caps), languages_(std::move(languages)) {
    if (languages_.empty()) languages_.push_back("en-US");
}

bool SettingsMenu::english() const { return prefs_.subtitles.language.rfind("pt", 0) != 0; }

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
    auto onoff = [this](bool b) { return std::string(b ? L("ligado", "on") : L("desligado", "off")); };
    auto soon  = [this](bool have, std::string v) { return have ? v : v + L(" (em breve)", " (coming soon)"); };
    auto camera_label = [this](st::CameraMode m) {
        return std::string(m == st::CameraMode::Original ? "Original Dreamcast" : L("Câmera Livre", "Free Camera"));
    };
    auto goTo = [](Page p) { return [p](SettingsMenu& m) { m.page_ = p, m.sel_ = 0; }; };
    // uma linha que abre outra pagina (sem valor de opcao: o desenho mostra ">")
    auto sub = [&](const char* label, Page p, bool enabled = true, std::string value = {}) {
        e.push_back({label, std::move(value), enabled, nullptr, goTo(p), true});
    };
    auto back = [&](Page p) { e.push_back({L("Voltar", "Back"), "", true, nullptr, goTo(p)}); };
    switch (page_) {
    case Page::Main:
        sub(L("Gráficos", "Graphics"), Page::Graphics);
        sub(L("Áudio", "Audio"), Page::Audio);
        sub(L("Legendas", "Subtitles"), Page::Subtitles);
        sub(L("Controles", "Controls"), Page::Controls);
        e.push_back({L("Restaurar padrões", "Restore defaults"), "", true, nullptr, [](SettingsMenu& m) {
                         m.pending_ = Pending::Restore;
                     }});
        e.push_back({L("Voltar ao jogo", "Return to game"), "", true, nullptr, [](SettingsMenu& m) { m.close(); }});
        break;
    case Page::Graphics: {
        const auto& g   = prefs_.graphics;
        const char* res[] = {"original (640x480)", "720p", "1080p", "1440p", "4K"};
        const char* asp[] = {L("4:3 (original)", "4:3 (original)"), "16:9", L("automático", "automatic")};
        const char* win[] = {L("janela", "windowed"), L("tela cheia", "fullscreen"), L("sem bordas", "borderless")};
        const char* flt[] = {"original", "bilinear", L("anisotrópica", "anisotropic")};
        e.push_back({L("Renderizador", "Renderer"), g.vulkan ? "Vulkan" : L("software (original)", "software (original)"), caps_.vulkan,
                     [](Preferences& p, int) { p.graphics.vulkan = !p.graphics.vulkan; }});
        // A "Predefinicao" saiu do menu (2026-10-23): com as opcoes individuais ela so mostrava "Personalizado". Cada
        // opcao continua aqui e no graficos.toml.
        // so as resolucoes que cabem no limite (caps.max_height) e so com o Vulkan (o software desenha na do jogo)
        int resolutions = 1;
        for (const uint32_t hh : {720u, 1080u, 1440u, 2160u}) resolutions += hh <= caps_.max_height;
        const bool        scaling  = f.scaling && g.vulkan;
        const std::string resValue = !f.scaling ? soon(false, res[size_t(g.resolution)])
                                     : g.vulkan ? std::string(res[size_t(g.resolution)])
                                                : std::string(res[size_t(g.resolution)]) + L(" (só com Vulkan)", " (Vulkan only)");
        e.push_back({L("Resolução interna", "Internal resolution"), resValue, scaling, [resolutions](Preferences& p, int d) {
                         p.graphics.resolution = cycle(p.graphics.resolution, d, resolutions);
                     }});
        e.push_back({L("Escala de renderização", "Render scale"), soon(f.scaling, pct(g.scale_pct)), scaling,
                     [](Preferences& p, int d) { p.graphics.scale_pct = step(p.graphics.scale_pct, d, 50, 200, 25); }});
        e.push_back({L("Proporção", "Aspect ratio"), soon(f.aspect_wide, asp[size_t(g.aspect)]), f.aspect_wide,
                     [](Preferences& p, int d) { p.graphics.aspect = cycle(p.graphics.aspect, d, 3); }});
        e.push_back({L("Modo de tela", "Display mode"), soon(f.window_modes, win[size_t(g.window)]), f.window_modes,
                     [](Preferences& p, int d) { p.graphics.window = cycle(p.graphics.window, d, 3); }});
        {
            static constexpr uint32_t sizes[][2] = {{0, 0}, {640, 480}, {960, 720}, {1280, 960}, {1440, 1080}, {1920, 1440}};
            const bool on = f.window_modes && g.window == st::WindowMode::Windowed;
            const std::string v = g.window_w ? std::to_string(g.window_w) + "x" + std::to_string(g.window_h) : L("padrão", "default");
            e.push_back({L("Tamanho da janela", "Window size"), soon(f.window_modes, v), on, [](Preferences& p, int d) {
                             int i = 0;
                             for (int k = 0; k < 6; ++k)
                                 if (sizes[k][0] == p.graphics.window_w && sizes[k][1] == p.graphics.window_h) i = k;
                             i = (i + d + 6) % 6;
                             p.graphics.window_w = sizes[i][0], p.graphics.window_h = sizes[i][1];
                         }});
        }
        e.push_back({"V-Sync", soon(f.vsync, onoff(g.vsync)), f.vsync, [](Preferences& p, int) { p.graphics.vsync = !p.graphics.vsync; }});
        e.push_back({L("Filtragem de texturas", "Texture filtering"), soon(f.filtering, flt[size_t(g.filter)]), f.filtering,
                     [](Preferences& p, int d) { p.graphics.filter = cycle(p.graphics.filter, d, 3); }});
        e.push_back({"Antialiasing (MSAA)", soon(f.msaa, g.msaa == 1 ? onoff(false) : std::to_string(g.msaa) + "x"), f.msaa,
                     [](Preferences& p, int d) {
                         const uint32_t v[] = {1, 2, 4, 8};
                         int            i   = int(std::find(v, v + 4, p.graphics.msaa) - v);
                         p.graphics.msaa    = v[std::clamp(i + d, 0, 3)];
                     }});
        e.push_back({L("Sombras", "Shadows"), soon(f.shadows, g.shadows == st::Shadows::Original ? "original" : L("aprimoradas", "enhanced")),
                     f.shadows, [](Preferences& p, int d) { p.graphics.shadows = cycle(p.graphics.shadows, d, 2); }});
        e.push_back({L("Iluminação aprimorada", "Enhanced lighting"), soon(f.lighting, onoff(g.lighting)), f.lighting,
                     [](Preferences& p, int) { p.graphics.lighting = !p.graphics.lighting; }});
        e.push_back({L("Texturas HD", "HD textures"), soon(f.hd_textures, onoff(g.hd_textures)), f.hd_textures,
                     [](Preferences& p, int) { p.graphics.hd_textures = !p.graphics.hd_textures; }});
        {  // N7c (secao 21): os efeitos de imagem numa pagina propria (submenu: sem valor; "em breve" sem nenhum efeito)
            const bool any = f.color || f.tonemap || f.sharpen || f.bloom || f.crt || f.post || f.shaders;
            sub(L("Efeitos de imagem", "Image effects"), Page::Effects, any, any ? std::string() : soon(false, ""));
        }
        back(Page::Main);
        break;
    }
    case Page::Effects: {
        // so a imagem do jogo; menu e legendas ficam por cima, sem efeito. Original Dreamcast = tudo desligado.
        const auto& g = prefs_.graphics;
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
        e.push_back({L("Correção de cor", "Color correction"), soon(f.color, onoff(g.color)), f.color,
                     [](Preferences& p, int) { p.graphics.color = !p.graphics.color; }});
        const bool cc = f.color && g.color;
        e.push_back({L("Brilho", "Brightness"), num(g.brightness, 2), cc, fstep([](Preferences& p) { return &p.graphics.brightness; }, 0.05f, -0.5f, 0.5f)});
        e.push_back({L("Contraste", "Contrast"), num(g.contrast, 2), cc, fstep([](Preferences& p) { return &p.graphics.contrast; }, 0.05f, 0.5f, 1.5f)});
        e.push_back({"Gamma", num(g.gamma, 2), cc, fstep([](Preferences& p) { return &p.graphics.gamma; }, 0.05f, 0.5f, 2.5f)});
        e.push_back({L("Saturação", "Saturation"), num(g.saturation, 2), cc,
                     fstep([](Preferences& p) { return &p.graphics.saturation; }, 0.05f, 0.f, 2.f)});
        e.push_back({"Tonemapping", soon(f.tonemap, onoff(g.tonemap)), f.tonemap,
                     [](Preferences& p, int) { p.graphics.tonemap = !p.graphics.tonemap; }});
        const bool tm = f.tonemap && g.tonemap;
        e.push_back({L("  Exposição (EV)", "  Exposure (EV)"), num(g.exposure, 2), tm,
                     fstep([](Preferences& p) { return &p.graphics.exposure; }, 0.1f, -2.f, 2.f)});
        e.push_back({L("  Altas luzes", "  Highlights"), num(g.highlights, 2), tm,
                     fstep([](Preferences& p) { return &p.graphics.highlights; }, 0.1f, 0.f, 1.f)});
        e.push_back({L("  Sombras", "  Shadows"), num(g.tone_shadows, 2), tm,
                     fstep([](Preferences& p) { return &p.graphics.tone_shadows; }, 0.05f, -0.5f, 0.5f)});
        e.push_back({L("Nitidez", "Sharpening"),
                     soon(f.sharpen, g.sharpen == 0 ? std::string(L("desligada", "off")) : pct(uint32_t(std::lround(g.sharpen * 100)))),
                     f.sharpen, fstep([](Preferences& p) { return &p.graphics.sharpen; }, 0.1f, 0.f, 1.f)});
        e.push_back({"Bloom", soon(f.bloom, onoff(g.bloom)), f.bloom, [](Preferences& p, int) { p.graphics.bloom = !p.graphics.bloom; }});
        const bool bl = f.bloom && g.bloom;
        e.push_back({L("  Limiar", "  Threshold"), num(g.bloom_threshold, 2), bl,
                     fstep([](Preferences& p) { return &p.graphics.bloom_threshold; }, 0.05f, 0.f, 1.f)});
        e.push_back({L("  Raio", "  Radius"), num(g.bloom_radius * 100, 1) + " %", bl,
                     fstep([](Preferences& p) { return &p.graphics.bloom_radius; }, 0.005f, 0.005f, 0.05f)});
        e.push_back({L("  Intensidade", "  Strength"), num(g.bloom_strength, 2), bl,
                     fstep([](Preferences& p) { return &p.graphics.bloom_strength; }, 0.1f, 0.f, 2.f)});
        e.push_back({"SSAO", soon(false, onoff(g.ssao)), false, nullptr});
        {
            const bool tall = st::internal_height(g) >= st::kCrtMinHeight;  // as linhas do jogo precisam de espaco
            e.push_back({L("Filtro CRT", "CRT filter"),
                         !f.crt ? soon(false, onoff(g.crt))
                         : tall ? onoff(g.crt)
                                : std::string(L("requer resolução interna 1080p ou mais", "requires 1080p internal resolution or higher")),
                         f.crt && tall, [](Preferences& p, int) { p.graphics.crt = !p.graphics.crt; }});
            const bool cr = f.crt && tall && g.crt;
            e.push_back({L("  Linhas de varredura", "  Scanlines"), num(g.crt_scanlines, 2), cr,
                         fstep([](Preferences& p) { return &p.graphics.crt_scanlines; }, 0.1f, 0.f, 1.f)});
            e.push_back({L("  Máscara", "  Mask"), num(g.crt_mask, 2), cr, fstep([](Preferences& p) { return &p.graphics.crt_mask; }, 0.1f, 0.f, 1.f)});
            e.push_back({L("  Vinheta", "  Vignette"), num(g.crt_vignette, 2), cr,
                         fstep([](Preferences& p) { return &p.graphics.crt_vignette; }, 0.1f, 0.f, 1.f)});
        }
        // shader de mod: "nenhum" ou um dos validos (os invalidos aparecem numa linha so, com o motivo no log)
        std::vector<const ShaderChoice*> valid;
        int                              invalid = 0;
        for (const auto& s : shaders_) s.error.empty() ? valid.push_back(&s) : void(++invalid);
        e.push_back({L("Shader de mod", "Mod shader"), soon(f.shaders, g.shader.empty() ? std::string(L("nenhum", "none")) : g.shader),
                     f.shaders && !valid.empty(), [valid](Preferences& p, int d) {
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
        if (invalid)
            e.push_back({L("Shaders inválidos", "Invalid shaders"),
                         std::to_string(invalid) + L(" (desativados; ver o log)", " (disabled; see the log)"), false, nullptr});
        back(Page::Graphics);
        break;
    }
    case Page::Audio: {
        const auto& a = prefs_.audio;
        auto        vol = [](uint32_t st::AudioSettings::*m) {
            return [m](Preferences& p, int d) { p.audio.*m = step(p.audio.*m, d, 0, 100, 5); };
        };
        e.push_back({L("Volume geral", "Master volume"), pct(a.master), true, vol(&st::AudioSettings::master)});
        e.push_back({L("Música", "Music"), pct(a.music), true, vol(&st::AudioSettings::music)});
        e.push_back({L("Efeitos", "Effects"), pct(a.effects), true, vol(&st::AudioSettings::effects)});
        e.push_back({L("Vozes", "Voices"), pct(a.voices), true, vol(&st::AudioSettings::voices)});
        back(Page::Main);
        break;
    }
    case Page::Subtitles: {
        const auto& s     = prefs_.subtitles;
        const auto  langs = languages_;
        // os idiomas dos pacotes instalados e "Desativadas" no fim da volta; o idioma da interface acompanha (desativadas:
        // o campo `language` nao muda, entao a interface fica no ultimo idioma)
        e.push_back({L("Idioma", "Language"), s.enabled ? language_label(s.language) : std::string(L("Desativadas", "Disabled")), true,
                     [langs](Preferences& p, int d) {
                         const int n  = int(langs.size()) + 1;
                         auto      it = std::find(langs.begin(), langs.end(), p.subtitles.language);
                         const int i  = !p.subtitles.enabled ? n - 1 : it == langs.end() ? 0 : int(it - langs.begin());
                         const int j  = ((i + d) % n + n) % n;
                         p.subtitles.enabled = j < n - 1;
                         if (p.subtitles.enabled) p.subtitles.language = langs[size_t(j)];
                     }});
        e.push_back({L("Tamanho do texto", "Text size"), pct(s.size_pct), s.enabled,
                     [](Preferences& p, int d) { p.subtitles.size_pct = step(p.subtitles.size_pct, d, 50, 200, 10); }});
        e.push_back({L("Contorno", "Outline"), onoff(s.outline), s.enabled, [](Preferences& p, int) {
                         p.subtitles.outline = !p.subtitles.outline;
                         if (!p.subtitles.outline && !p.subtitles.box) p.subtitles.box = true;  // um dos dois sempre
                     }});
        e.push_back({L("Fundo", "Background"), onoff(s.box), s.enabled, [](Preferences& p, int) {
                         p.subtitles.box = !p.subtitles.box;
                         if (!p.subtitles.outline && !p.subtitles.box) p.subtitles.outline = true;
                     }});
        // so quando algum pacote instalado diz quem fala (nunca por suposicao); senao fica indisponivel (o motivo, nenhum
        // pacote com o campo "personagem", nao cabe na linha: docs/LEGENDAS-PENDENCIAS.md)
        e.push_back({L("Identificação do personagem", "Speaker names"),
                     speakers_ ? onoff(s.speaker) : std::string(L("indisponível", "unavailable")),
                     s.enabled && speakers_, [](Preferences& p, int) { p.subtitles.speaker = !p.subtitles.speaker; }});
        back(Page::Main);
        break;
    }
    case Page::Controls:
        sub(L("Câmera", "Camera"), Page::Camera, true, camera_label(prefs_.camera.mode));
        for (const MenuItem& i : controlsInfo_) e.push_back({i.label, i.value, true, nullptr, nullptr});
        e.push_back({L("Remapear pelo menu", "Remap from the menu"), L("em breve (controles.toml)", "coming soon (controles.toml)"), false,
                     nullptr, nullptr});
        back(Page::Main);
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
        e.push_back({L("Modo", "Mode"), camera_label(c.mode), true, [](Preferences& p, int d) { p.camera.mode = cycle(p.camera.mode, d, 2); }});
        // no Original nao ha deslocamento: o movimento e sempre o do console
        e.push_back({L("Movimentação", "Movement"),
                     !on || c.movement == st::MoveMode::Original ? "Original" : L("Relativa à câmera", "Camera-relative"), on,
                     [](Preferences& p, int) {
                         p.camera.movement = p.camera.movement == st::MoveMode::Camera ? st::MoveMode::Original : st::MoveMode::Camera;
                     }});
        e.push_back({L("Sensibilidade horizontal", "Horizontal sensitivity"), num(c.sens_x, 0, " °/s"), on,
                     fstep(&st::CameraSettings::sens_x, 10, 10, 360)});
        e.push_back({L("Sensibilidade vertical", "Vertical sensitivity"), num(c.sens_y, 0, " °/s"), on,
                     fstep(&st::CameraSettings::sens_y, 10, 10, 360)});
        e.push_back({L("Zona morta", "Dead zone"), num(c.deadzone, 2, ""), on, fstep(&st::CameraSettings::deadzone, 0.05f, 0, 0.6f)});
        e.push_back({L("Inverter eixo Y", "Invert Y axis"), onoff(c.invert_y), on, [](Preferences& p, int) { p.camera.invert_y = !p.camera.invert_y; }});
        e.push_back({L("Suavização", "Smoothing"), num(c.smoothing, 1, ""), on, fstep(&st::CameraSettings::smoothing, 0.1f, 0, 0.9f)});
        e.push_back({L("Recentralizar com R3", "Recenter with R3"), onoff(c.r3_recenter), on,
                     [](Preferences& p, int) { p.camera.r3_recenter = !p.camera.r3_recenter; }});
        e.push_back({L("Limite para cima", "Upward limit"), num(c.pitch_up, 0, "°"), on, fstep(&st::CameraSettings::pitch_up, 5, 0, 45)});
        e.push_back({L("Limite para baixo", "Downward limit"), num(c.pitch_down, 0, "°"), on, fstep(&st::CameraSettings::pitch_down, 5, 0, 45)});
        back(Page::Controls);
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
                // Original Dreamcast, volumes 100, legendas ligadas; o idioma escolhido fica (ele e tambem o da interface)
                Preferences def;
                def.subtitles.language = prefs_.subtitles.language;
                commit(def, now);
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
        if (e[size_t(sel_)].action) {
            if (e[size_t(sel_)].enabled) e[size_t(sel_)].action(*this);
        } else {
            change(+1, now);
        }
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
    MenuView          v;
    const std::string root = L("Configurações", "Settings"), gr = L("Gráficos", "Graphics"),
                      ct = L("Controles", "Controls"), sep = " › ";
    const std::string titles[] = {root,
                                  root + sep + gr,
                                  root + sep + L("Áudio", "Audio"),
                                  root + sep + L("Legendas", "Subtitles"),
                                  root + sep + ct,
                                  root + sep + gr + sep + L("Efeitos de imagem", "Image effects"),
                                  root + sep + ct + sep + L("Câmera", "Camera")};
    v.title = titles[size_t(page_)];
    for (const auto& e : entries()) v.items.push_back({e.label, e.value, e.enabled, e.submenu});
    v.selected = sel_;
    v.help     = L("Setas / D-pad: navegar     ◀ ▶: alterar     Enter / A: escolher     Esc / B: voltar",
                   "Arrows / D-pad: navigate     ◀ ▶: change     Enter / A: select     Esc / B: back");
    if (pending_ == Pending::Display) {
        const int left = std::max(0, int(std::ceil(deadline_ - now_)));
        v.prompt = english() ? "Keep this display setting? It reverts automatically in " + std::to_string(left) +
                                   " s. (Enter/A = keep, Esc/B = undo)"
                             : "Manter esta configuração de tela? Volta sozinha em " + std::to_string(left) +
                                   " s. (Enter/A = manter, Esc/B = desfazer)";
    }
    if (pending_ == Pending::Restore)
        v.prompt = L("Restaurar todas as configurações padrão? (Enter/A = sim, Esc/B = não)",
                     "Restore all default settings? (Enter/A = yes, Esc/B = no)");
    return v;
}

} // namespace bs
