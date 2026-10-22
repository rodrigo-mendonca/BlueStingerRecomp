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
#include "render/post_shader.h"

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

// N7c (secao 21): os efeitos de imagem pedidos (so a imagem do jogo, antes das camadas). Tudo desligado e sem shader =
// nenhum passe (o caminho de sempre: Original Dreamcast).
struct PostSpec {
    bool  color = false;
    float brightness = 0, contrast = 1, gamma = 1, saturation = 1;
    const native::render::PostShaderPackage* shader = nullptr;  // shader de mod (ja validado) ou nenhum
    std::vector<float>                       shaderParams;      // na ordem dos parametros do manifesto
    bool operator==(const PostSpec&) const = default;
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
    // Os efeitos (so a apresentacao Vulkan os aplica). Devolve vazio, ou o motivo de o shader de mod nao poder ser usado
    // (o driver recusou o modulo ou o pipeline): quem chama o desativa.
    virtual std::string set_post(const PostSpec&) { return {}; }
    // Um shader de mod que deixou de poder ser usado depois de set_post (recarga num dispositivo novo recusada, ou perda
    // do dispositivo com ele ativo): o motivo, uma vez; vazio = nada. Quem chama o desativa como em set_post.
    virtual std::string take_post_error() { return {}; }
};

std::unique_ptr<WindowPresenter> make_sdl_presenter(SDL_Window* window, SDL_Renderer* renderer);
// window criada com SDL_WINDOW_VULKAN e o dispositivo compartilhado configurado com as extensoes da superficie
// (native::render::vulkan_shared_configure); nullptr com o motivo em *error.
std::unique_ptr<WindowPresenter> make_vulkan_presenter(SDL_Window* window, std::string* error);

} // namespace bs
