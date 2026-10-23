#pragma once
// N7c (docs/N7-PLANO.md secoes 17 e 19): o MODELO do menu interno de configuracoes. Estado e navegacao, sem desenho
// nem janela: quem desenha le view() (camada nativa, por cima do jogo, na resolucao de saida) e quem le a entrada
// traduz controle Xbox, teclado e mouse em MenuAction. As preferencias sao as de native/platform (graficas, audio,
// legendas); controles abre o mapeamento do N3 (o menu so pede isso de volta, nao o reimplementa).
//
// Regras:
//   - opcao que o renderizador ainda nao implementa (Capabilities/Features) aparece desativada ("em breve");
//   - mudanca aplicavel na hora vira pendente de aplicacao (take_changes) imediatamente;
//   - mudanca de tamanho/modo de tela (requires_confirmation) aplica e pede confirmacao: sem ela em 15 s (ou com
//     Voltar), a configuracao anterior volta sozinha;
//   - "Restaurar padroes" pede confirmacao e volta tudo ao padrao (Original Dreamcast, volumes 100, legendas en-US).
#include "platform/camera_settings.h"
#include "platform/graphics_settings.h"
#include "platform/user_settings.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bs {

struct Preferences {
    native::settings::GraphicsSettings graphics;
    native::settings::AudioSettings    audio;
    native::settings::SubtitleSettings subtitles;
    native::settings::CameraSettings   camera;  // N7c: camera livre ([camera] do controles.toml)
    bool                               operator==(const Preferences&) const = default;
};

enum class MenuAction : uint8_t { Up, Down, Left, Right, Confirm, Back };

struct MenuItem {
    std::string label, value;
    bool        enabled = true;  // false: indisponivel (mostrado, nao alteravel)
};

// N7c (secao 21): um shader de pos-processamento de mod encontrado (mods\shaders), com os parametros do manifesto;
// error nao vazio = invalido (o menu o mostra, mas nao deixa escolher).
struct ShaderChoice {
    struct Param {
        std::string name, label;
        float       min = 0, max = 1, def = 0, step = 0.05f;
    };
    std::string        name, description, error;
    std::vector<Param> params;
};

struct MenuView {
    std::string           title;
    std::vector<MenuItem> items;
    int                   selected = 0;
    std::string           prompt;  // confirmacao em curso ("Manter esta configuracao? 12 s") ou vazio
};

class SettingsMenu {
public:
    SettingsMenu(Preferences prefs, native::settings::Capabilities caps, std::vector<std::string> languages);

    void open();
    void close();
    bool is_open() const { return open_; }

    void input(MenuAction a, double now_seconds);
    void tick(double now_seconds);  // a reversao automatica da confirmacao
    // mouse: leva a selecao a um item (sem efeito com uma pergunta aberta) e muda o valor dele (-1 / +1)
    void select(int index);
    void adjust(int index, int dir, double now_seconds);

    MenuView           view() const;
    const Preferences& prefs() const { return prefs_; }
    // O que mudou desde a ultima chamada (para aplicar e gravar); false se nada.
    bool take_changes(Preferences& out);
    // A pagina Controles mostra o mapeamento atual do N3 (rotulo = acao, valor = teclas e botoes); remapear pelo menu
    // ainda nao existe (aparece "em breve").
    void set_controls_info(std::vector<MenuItem> info) { controlsInfo_ = std::move(info); }
    // Os shaders de mod encontrados (pagina Efeitos); mark_shader_invalid: um que falhou ao carregar na GPU.
    void set_shaders(std::vector<ShaderChoice> s) { shaders_ = std::move(s); }
    // algum pacote de legendas instalado diz quem fala (campo "personagem"): a opcao de identificacao fica disponivel
    void set_speakers_available(bool on) { speakers_ = on; }
    void mark_shader_invalid(const std::string& name, const std::string& why);
    const std::vector<ShaderChoice>& shaders() const { return shaders_; }

    // Confirmacao de tela em curso (a mudanca ja aplicada, ainda nao aceita) e o desfazer dela quando quem aplica falha.
    bool display_pending() const { return pending_ == Pending::Display; }
    void cancel_display();

    enum class Page : uint8_t { Main, Graphics, Audio, Subtitles, Controls, Effects, Camera };
    Page page() const { return page_; }

private:
    struct Entry;
    std::vector<Entry> entries() const;
    void               change(int dir, double now);
    void               commit(const Preferences& next, double now);

    Preferences                      prefs_, defaults_;
    native::settings::Capabilities   caps_;
    std::vector<std::string>         languages_;
    bool                             speakers_ = false;
    bool                             open_ = false, dirty_ = false;
    std::vector<MenuItem>            controlsInfo_;
    std::vector<ShaderChoice>        shaders_;
    Page                             page_ = Page::Main;
    int                              sel_  = 0;
    // confirmacao: de tamanho/modo de tela (reverte para `before_`) ou de restaurar padroes
    enum class Pending : uint8_t { None, Display, Restore } pending_ = Pending::None;
    Preferences                      before_;
    double                           deadline_ = 0, now_ = 0;  // now_: o ultimo instante visto (contagem)
};

} // namespace bs
