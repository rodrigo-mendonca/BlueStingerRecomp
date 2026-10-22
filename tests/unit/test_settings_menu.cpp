// N7c: modelo do menu interno de configuracoes (app/settings_menu): navegacao, opcoes indisponiveis, aplicacao,
// confirmacao com reversao automatica, restaurar padroes e o pedido dos controles. Sem janela nem desenho.
#include "test.h"

#include "settings_menu.h"

#include <string>

namespace st = bs::native::settings;
using bs::MenuAction;

namespace {
int find(const bs::MenuView& v, const std::string& label) {
    for (size_t i = 0; i < v.items.size(); ++i)
        if (v.items[i].label == label) return int(i);
    return -1;
}
// leva a selecao ate o item (para baixo)
void go(bs::SettingsMenu& m, const std::string& label, double t = 0) {
    for (int i = 0; i < 40 && m.view().items[size_t(m.view().selected)].label != label; ++i) m.input(MenuAction::Down, t);
}
} // namespace

BS_TEST(settings_menu_navigation_and_unavailable_options) {
    st::Capabilities caps;
    caps.vulkan = true, caps.max_msaa = 8, caps.max_anisotropy = 16, caps.max_height = 2160;  // so o modo fiel existe
    bs::SettingsMenu m({}, caps, {"en-US", "pt-BR"});
    BS_CHECK(!m.is_open());
    m.open();
    BS_CHECK(m.is_open() && m.page() == bs::SettingsMenu::Page::Main);
    const auto main = m.view();
    for (const char* l : {"Gr\u00e1ficos", "\u00c1udio", "Legendas", "Controles", "Restaurar padr\u00f5es", "Voltar ao jogo"})
        BS_CHECK(find(main, l) >= 0);
    go(m, "Gr\u00e1ficos");
    m.input(MenuAction::Confirm, 0);
    BS_CHECK(m.page() == bs::SettingsMenu::Page::Graphics);
    const auto g = m.view();
    // nada implementado alem do fiel: as opcoes de melhoria aparecem indisponiveis ("em breve") e nao mudam
    for (const char* l : {"Predefini\u00e7\u00e3o", "Resolu\u00e7\u00e3o interna", "Antialiasing (MSAA)", "Bloom", "Filtro CRT"}) {
        const int i = find(g, l);
        BS_CHECK(i >= 0 && !g.items[size_t(i)].enabled && g.items[size_t(i)].value.find("em breve") != std::string::npos);
    }
    go(m, "Bloom");
    m.input(MenuAction::Right, 0);
    BS_CHECK(!m.prefs().graphics.bloom);
    bs::Preferences out;
    BS_CHECK(!m.take_changes(out));
    m.input(MenuAction::Back, 0);
    BS_CHECK(m.page() == bs::SettingsMenu::Page::Main);
    m.input(MenuAction::Back, 0);
    BS_CHECK(!m.is_open());
}

BS_TEST(settings_menu_audio_subtitles_and_changes) {
    bs::SettingsMenu m({}, {}, {"en-US", "pt-BR"});
    m.open();
    go(m, "\u00c1udio");
    m.input(MenuAction::Confirm, 0);
    go(m, "M\u00fasica");
    m.input(MenuAction::Left, 0), m.input(MenuAction::Left, 0);
    bs::Preferences out;
    BS_CHECK(m.take_changes(out) && out.audio.music == 90 && out.audio.master == 100);
    BS_CHECK(!m.take_changes(out));  // so uma vez
    m.input(MenuAction::Back, 0);
    go(m, "Legendas");
    m.input(MenuAction::Confirm, 0);
    BS_CHECK(m.prefs().subtitles.enabled && m.prefs().subtitles.language == "en-US");  // ingles ligado por padrao
    go(m, "Idioma");
    m.input(MenuAction::Right, 0);
    BS_CHECK(m.prefs().subtitles.language == "pt-BR");
    go(m, "Tamanho do texto");
    m.input(MenuAction::Right, 0);
    BS_CHECK(m.prefs().subtitles.size_pct == 110);
    go(m, "Contorno");
    m.input(MenuAction::Confirm, 0);  // sem contorno: o fundo liga (um dos dois sempre)
    BS_CHECK(!m.prefs().subtitles.outline && m.prefs().subtitles.box);
    go(m, "Legendas");
    m.input(MenuAction::Confirm, 0);
    BS_CHECK(!m.prefs().subtitles.enabled && m.prefs().subtitles.effective_language().empty());
    BS_CHECK(m.take_changes(out) && !out.subtitles.enabled);
}

BS_TEST(settings_menu_display_confirmation_and_revert) {
    st::Capabilities caps;
    caps.vulkan = true, caps.max_msaa = 8, caps.max_anisotropy = 16, caps.max_height = 2160, caps.features.window_modes = true;
    bs::SettingsMenu m({}, caps, {"en-US"});
    m.open();
    go(m, "Gr\u00e1ficos");
    m.input(MenuAction::Confirm, 0);
    go(m, "Modo de tela");
    m.input(MenuAction::Right, 1.0);  // tela cheia: aplica e pede confirmacao
    BS_CHECK(m.prefs().graphics.window == st::WindowMode::Fullscreen && !m.view().prompt.empty());
    m.tick(1.0 + st::kConfirmSeconds - 0.1);
    BS_CHECK(m.prefs().graphics.window == st::WindowMode::Fullscreen);
    m.tick(1.0 + st::kConfirmSeconds + 0.1);  // sem resposta: volta sozinho
    BS_CHECK(m.prefs().graphics.window == st::WindowMode::Windowed && m.view().prompt.empty());
    m.input(MenuAction::Right, 30.0);
    m.input(MenuAction::Confirm, 31.0);  // confirmado: fica
    m.tick(100.0);
    BS_CHECK(m.prefs().graphics.window == st::WindowMode::Fullscreen);
    m.input(MenuAction::Right, 101.0);  // sem bordas, e Voltar desfaz
    BS_CHECK(m.prefs().graphics.window == st::WindowMode::Borderless);
    m.input(MenuAction::Back, 102.0);
    BS_CHECK(m.prefs().graphics.window == st::WindowMode::Fullscreen);
    m.input(MenuAction::Right, 103.0);  // fechar o menu sem confirmar tambem desfaz
    m.close();
    BS_CHECK(m.prefs().graphics.window == st::WindowMode::Fullscreen);
}

BS_TEST(settings_menu_restore_defaults_and_controls) {
    bs::Preferences p;
    p.audio.master = 40, p.subtitles.enabled = false, p.subtitles.size_pct = 180;
    bs::SettingsMenu m(p, {}, {"en-US"});
    m.set_controls_info({{"A", "Z / a"}, {"START", "ENTER / start"}});
    m.open();
    go(m, "Restaurar padr\u00f5es");
    m.input(MenuAction::Confirm, 0);
    BS_CHECK(!m.view().prompt.empty() && m.prefs().audio.master == 40);  // ainda pergunta
    m.input(MenuAction::Back, 0);
    BS_CHECK(m.prefs().audio.master == 40 && m.view().prompt.empty());  // cancelado
    m.input(MenuAction::Confirm, 0);
    m.input(MenuAction::Confirm, 0);
    BS_CHECK(m.prefs() == bs::Preferences{});
    go(m, "Controles");
    m.input(MenuAction::Confirm, 0);
    const auto c = m.view();  // o mapeamento atual do N3 e o remapeamento ainda indisponivel
    BS_CHECK(find(c, "A") >= 0 && c.items[size_t(find(c, "A"))].value == "Z / a");
    BS_CHECK(find(c, "Remapear pelo menu") >= 0 && !c.items[size_t(find(c, "Remapear pelo menu"))].enabled);
}
