// N7c: o desenho do menu interno (app/menu_overlay): a camada tem o painel visivel por cima do jogo, as linhas na ordem,
// e o mouse acha a linha e as setas (as areas que a janela usa para clique e passagem). Sem Direct2D: [skip].
#include "test.h"

#include "menu_overlay.h"

#include <set>
#include <vector>

BS_TEST(menu_overlay_draws_panel_and_hit_areas) {
    bs::MenuOverlay o;
    if (!o.ok()) BS_SKIP("Direct2D/DirectWrite");
    bs::MenuView v;
    v.title    = "Configurações > Áudio";
    v.items    = {{"Volume geral", "90 %", true}, {"Música", "100 %", true}, {"Em breve", "x (em breve)", false}, {"Voltar", "", true}};
    v.selected = 1;
    const uint32_t w = 1280, h = 960;
    const auto&    l = o.layer(v, w, h, 160, 0, 960, 960);  // area 4:3 dentro de uma saida mais larga
    BS_CHECK(l.size() == size_t(w) * h);
    // fora da area do jogo nada e desenhado; dentro, o painel (opaco o bastante) e o texto
    BS_CHECK((l[size_t(480) * w + 40] >> 24) == 0);
    BS_CHECK((l[size_t(480) * w + 640] >> 24) > 0x80);
    size_t white = 0;
    for (uint32_t px : l) white += (px & 0xFFFFFF) >= 0xE0E0E0;
    BS_CHECK(white > 500);
    // varrendo a coluna dos rotulos de cima para baixo: as linhas na ordem
    std::vector<int> order;
    for (float y = 0; y < float(h); y += 2)
        if (auto hit = o.hit(300, y); hit && hit->part == bs::MenuHit::Part::Row && (order.empty() || order.back() != hit->item))
            order.push_back(hit->item);
    BS_CHECK((order == std::vector<int>{0, 1, 2, 3}));
    // as setas so nas linhas que mudam (habilitadas e com valor)
    std::set<std::pair<int, int>> arrows;
    for (float y = 0; y < float(h); y += 2)
        for (float x = 640; x < 1120; x += 4)
            if (auto hit = o.hit(x, y); hit && hit->part != bs::MenuHit::Part::Row) arrows.insert({hit->item, int(hit->part)});
    BS_CHECK(arrows.count({0, int(bs::MenuHit::Part::Decrease)}) && arrows.count({0, int(bs::MenuHit::Part::Increase)}));
    BS_CHECK(arrows.count({1, int(bs::MenuHit::Part::Increase)}) && !arrows.count({2, int(bs::MenuHit::Part::Increase)}));
    BS_CHECK(!arrows.count({3, int(bs::MenuHit::Part::Increase)}));
    // com uma pergunta aberta, nenhuma linha recebe o mouse (o clique so confirma)
    v.prompt = "Manter esta configuração de tela?";
    o.layer(v, w, h, 160, 0, 960, 960);
    BS_CHECK(!o.hit(300, 200));
}
