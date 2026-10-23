#pragma once
// O cursor do mouse na janela do jogo (2026-10-23, pedido do mantenedor). So no modo janela: escondido enquanto o
// jogo tem o foco e o menu de configuracoes esta fechado (gameplay); visivel com o menu aberto (o mouse navega) e sem
// o foco (Alt+Tab, clique em outro programa). Em tela cheia (exclusiva ou sem bordas) nada muda: o comportamento de
// antes, cursor visivel. Sem confinar o cursor nem movimento relativo. Ao fechar, o jogo devolve o cursor visivel.

namespace bs {

enum class CursorWhy { Gameplay, Menu, NoFocus, Fullscreen };

inline CursorWhy cursor_state(bool fullscreen, bool focused, bool menuOpen) {
    if (fullscreen) return CursorWhy::Fullscreen;
    if (!focused) return CursorWhy::NoFocus;
    return menuOpen ? CursorWhy::Menu : CursorWhy::Gameplay;
}
inline bool cursor_hidden(CursorWhy w) { return w == CursorWhy::Gameplay; }
inline const char* cursor_why_name(CursorWhy w) {
    switch (w) {
    case CursorWhy::Gameplay: return "oculto (janela com foco, gameplay)";
    case CursorWhy::Menu: return "visivel (menu aberto)";
    case CursorWhy::NoFocus: return "visivel (sem foco)";
    default: return "visivel (tela cheia)";
    }
}

} // namespace bs
