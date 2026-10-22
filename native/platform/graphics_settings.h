#pragma once
// N7 (docs/N7-PLANO.md secoes 15 a 17): modelo das configuracoes graficas, separado da interface (o menu interno
// do jogo, depois do backend Vulkan estabilizar) e do renderizador (que so recebe o resultado). Persistencia em
// graficos.toml na pasta do usuario (ao lado de controles.toml); o mesmo padrao de configuracao preparado para
// audio, controles e acessibilidade.
//
// Presets: Original Dreamcast (a configuracao de equivalencia: resolucao do jogo, 4:3, amostragem original, sem
// efeitos), Equilibrado, Alta Qualidade e Personalizado (qualquer combinacao que nao seja um preset). O modo de
// tela, o V-Sync e a proporcao sao do jogador e nao fazem parte dos presets.
// Capacidades: o backend informa o que suporta e o que JA esta implementado (Features); clamp_to() leva a configuracao
// para o suportado (com o motivo), e sem Vulkan tudo cai no Original (o renderizador em software so faz o original).
// O menu so oferece como funcionando o que Features diz (nada de opcao de enfeite).
// Aplicacao (apply_kind): filtragem e efeitos valem no quadro seguinte; resolucao, escala, MSAA, modo de tela,
// proporcao, tamanho da janela e V-Sync recriam alvos/swapchain entre quadros (sem tocar o estado do jogo); as que
// mudam o tamanho ou o modo de tela pedem confirmacao com reversao automatica (requires_confirmation).
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace bs::native::settings {

inline constexpr const wchar_t* kGraphicsFile   = L"graficos.toml";
inline constexpr int            kConfirmSeconds = 15;  // mudanca de resolucao/modo de tela sem confirmacao: volta

enum class Preset : uint8_t { Original, Balanced, High, Custom };
enum class Resolution : uint8_t { Original, P720, P1080, P1440, P2160 };  // altura interna 480/720/1080/1440/2160
enum class WindowMode : uint8_t { Windowed, Fullscreen, Borderless };
enum class Aspect : uint8_t { Original4x3, Wide16x9, Auto };  // Auto: 16:9 so se a janela for larga e o HUD corrigido
enum class Filter : uint8_t { Original, Bilinear, Anisotropic };
enum class Shadows : uint8_t { Original, Enhanced };

struct GraphicsSettings {
    // do preset
    Resolution resolution  = Resolution::Original;
    uint32_t   scale_pct   = 100;  // escala sobre a resolucao interna (50 a 200)
    Filter     filter      = Filter::Original;
    uint32_t   anisotropy  = 16;   // 2 a 16 (so com Filter::Anisotropic)
    uint32_t   msaa        = 1;    // 1, 2, 4, 8
    bool       hd_textures = false;  // substituicoes HD por conteudo, quando houver pacote (N9)
    bool       lighting    = false;  // iluminacao aprimorada (so com dados confiaveis)
    Shadows    shadows     = Shadows::Original;
    std::string shader;              // "" = os shaders originais; outro = conjunto configuravel (mods no N9)
    bool       bloom = false, ssao = false, tonemap = false, crt = false;
    float      sharpen = 0, gamma = 1.0f, saturation = 1.0f;
    // do jogador (fora dos presets)
    WindowMode window = WindowMode::Windowed;
    uint32_t   window_w = 0, window_h = 0;  // tamanho da janela (0 = o padrao); tela cheia usa a do monitor
    Aspect     aspect = Aspect::Original4x3;
    bool       vsync  = true;
    bool       operator==(const GraphicsSettings&) const = default;
};

// O que o renderizador ja implementa (N7c gradual): o resto fica indisponivel no menu e volta ao valor original.
struct Features {
    bool scaling = false, msaa = false, filtering = false, window_modes = false, vsync = false, aspect_wide = false;
    bool hd_textures = false, lighting = false, shadows = false, shaders = false, post = false;
    static Features all() { return {true, true, true, true, true, true, true, true, true, true, true}; }
};

struct Capabilities {
    bool     vulkan         = false;
    Features features;
    uint32_t max_msaa       = 1;
    uint32_t max_anisotropy = 1;
    uint32_t max_height     = 480;  // maior altura interna que cabe na memoria/limites da GPU
    bool     wide_hud       = false;  // correcao do HUD em 16:9 disponivel
};

GraphicsSettings preset(Preset p);                   // o preset (Custom = Original)
Preset           classify(const GraphicsSettings& s);  // qual preset a configuracao e (ou Custom)
uint32_t         internal_height(const GraphicsSettings& s);  // altura interna em pixels, com a escala

// O que o renderizador consegue: cada opcao sem suporte volta ao valor seguro, com o motivo em *notes.
GraphicsSettings clamp_to(const GraphicsSettings& s, const Capabilities& caps, std::vector<std::string>* notes = nullptr);

// Mudanca que pede confirmacao com reversao automatica (resolucao, escala, modo de tela, proporcao).
bool requires_confirmation(const GraphicsSettings& from, const GraphicsSettings& to);

enum class Apply : uint8_t { None, Immediate, Recreate };  // nada mudou / no proximo quadro / recriar alvos e swapchain
Apply apply_kind(const GraphicsSettings& from, const GraphicsSettings& to);

std::string      to_toml(const GraphicsSettings& s);
GraphicsSettings from_toml(const std::string& text, std::string* warning = nullptr);  // invalido: padrao + aviso
GraphicsSettings read_graphics(const std::filesystem::path& dir, std::string* warning = nullptr);  // ausente: Original
bool write_graphics(const std::filesystem::path& dir, const GraphicsSettings& s, std::string* err = nullptr,
                    std::error_code* code = nullptr);

const char* preset_name(Preset p);  // "original", "equilibrado", "alta", "personalizado"

} // namespace bs::native::settings
