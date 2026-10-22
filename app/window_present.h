#pragma once
// N7c: a apresentacao da janela (docs/N7-PLANO.md secao 20), separada do laco da janela e do menu:
//   - Vulkan (caminho B, padrao com GPU): o MESMO dispositivo do renderizador desenha na janela (swapchain propria). A
//     imagem na resolucao interna (GameFrame::hiresSeq) e desenhada direto da GPU; a imagem da CPU (software, filmes,
//     telas escritas pela CPU) e enviada so quando chega um quadro novo; as camadas (menu, legendas), so quando mudam.
//   - SDL (recuo: sem Vulkan, --apresentacao sdl, ou a apresentacao Vulkan falhou): o SDL_Renderer com a imagem na
//     resolucao do jogo (a VRAM emulada).
// Os dois desenham o mesmo: fundo preto, a imagem do jogo na area 4:3 (game_area) e as camadas por cima, em pixels da
// saida (texto nitido em qualquer tamanho).
#include "game_host.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bs {

// Camada sobre a saida inteira: ARGB pre-multiplicado, width x height = o tamanho da saida. O indice e fixo (0 legendas,
// 1 menu; argb nulo = ausente): a versao identifica o conteudo so dentro do mesmo indice.
struct WindowLayer {
    const std::vector<uint32_t>* argb = nullptr;
    uint32_t                     width = 0, height = 0;
    uint64_t                     version = 0;  // muda quando o conteudo muda
};

// A area do jogo na saida w x h: o maior retangulo 4:3 centrado (faixas pretas nas sobras).
SDL_FRect game_area(int w, int h);

class WindowPresenter {
public:
    virtual ~WindowPresenter()        = default;
    virtual const char* name() const  = 0;  // "Vulkan" ou "SDL"
    virtual void        output_size(int& w, int& h) = 0;
    virtual void        set_vsync(bool on)          = 0;
    // Desenha e apresenta. frameSeq muda a cada quadro novo do jogo; capture != nullptr recebe a imagem apresentada
    // (ARGB, o tamanho da saida). false = falhou (o chamador conta as falhas seguidas e pode recuar para o SDL).
    virtual bool present(const GameFrame& f, uint64_t frameSeq, const SDL_FRect& area, std::span<const WindowLayer> layers,
                         std::vector<uint32_t>* capture) = 0;
    virtual std::string stats() const { return {}; }  // resumo do custo (log no fim)
};

std::unique_ptr<WindowPresenter> make_sdl_presenter(SDL_Window* window, SDL_Renderer* renderer);
// window criada com SDL_WINDOW_VULKAN e o dispositivo compartilhado configurado com as extensoes da superficie
// (native::render::vulkan_shared_configure); nullptr com o motivo em *error.
std::unique_ptr<WindowPresenter> make_vulkan_presenter(SDL_Window* window, std::string* error);

} // namespace bs
