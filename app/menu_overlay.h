#pragma once
// N7c: o DESENHO do menu interno (o estado e a navegacao sao de app/settings_menu.h). Uma camada transparente (ARGB
// pre-multiplicado) na resolucao de saida, por cima do jogo e das legendas, dentro da area do jogo (a imagem 4:3):
// painel translucido, titulo, linhas com rotulo e valor ("< valor >" quando muda), selecao realcada, opcoes
// indisponiveis apagadas, rolagem quando a pagina nao cabe, a caixa de confirmacao e a linha de ajuda. Direct2D e
// DirectWrite (os mesmos das legendas). Tambem diz o que esta sob o mouse (linha e setas).
#include "settings_menu.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bs {

struct MenuHit {
    int item = -1;
    enum class Part : uint8_t { Row, Decrease, Increase } part = Part::Row;
};

class MenuOverlay {
public:
    MenuOverlay();
    ~MenuOverlay();
    MenuOverlay(const MenuOverlay&)            = delete;
    MenuOverlay& operator=(const MenuOverlay&) = delete;
    bool ok() const;

    // A camada w x h com o menu na area (cx, cy, cw, ch); refeita so quando o conteudo, o tamanho ou a area mudam.
    const std::vector<uint32_t>& layer(const MenuView& v, uint32_t w, uint32_t h, float cx, float cy, float cw, float ch);
    // O item (e a parte) no ponto (x, y) da ultima camada desenhada.
    std::optional<MenuHit> hit(float x, float y) const;
    uint64_t               version() const;  // muda a cada camada redesenhada

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bs
