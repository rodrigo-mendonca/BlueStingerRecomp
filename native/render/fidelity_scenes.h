#pragma once
// N7 (preparacao): cenas sinteticas de fidelidade visual na IR (render.h), com pontos de prova e o valor
// esperado de cada um, para o backend Vulkan (e qualquer outro) provar as regras do PVR2 sem o jogo.
// As regras e os valores seguem a referencia em software (runtime/pvr/pvr_render.cpp) e os testes dela
// (tests/unit/test_render_formats.cpp): autosort por pixel com >= forcado, punch-through com alfa >= ref e
// ordem da frente para tras, profundidade 1/w com >=, mistura SrcAlpha/InvSrcAlpha e One/One, cor base
// modulada + offset, HUD translucido, quadro de video pronto.
//
// Modo FIEL: o que a comparacao com a referencia e o Flycast exige. Modo MELHORIA: opcional, nunca
// confundido com o fiel (ex.: ordenar translucidas por triangulo; so como opcao nao fiel).
// Cena BLOQUEADA: sem valor esperado confiavel ainda (motivo no campo `blocked`); o teste exige o motivo,
// nunca passa em silencio.
#include "render/render.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bs::native::render {

enum class SceneMode : uint8_t { Faithful, Enhancement };

struct Probe {
    uint32_t x = 0, y = 0;
    uint32_t rgb = 0;                 // esperado (0x00RRGGBB; o alfa do alvo nao e comparado)
    uint8_t  tolerance = 2;           // por canal (arredondamento da mistura no GPU)
    int64_t  rgb_if_triangle_sorted = -1;  // >= 0: o que uma ordenacao POR TRIANGULO daria (discriminador)
};

struct SceneTexture {
    TextureDesc           desc;  // ARGB8888 ja convertido (texture_convert)
    std::vector<uint32_t> argb;
};

struct FidelityScene {
    std::string                name;
    std::string                rule;  // a regra do PVR2 que a cena prova (e a fonte)
    SceneMode                  mode = SceneMode::Faithful;
    bool                       image = false;  // quadro pronto (present_image): sem geometria
    CommandList                cl;
    std::vector<int32_t>       draw_texture;  // por desenho: indice em textures (-1: sem textura)
    std::vector<SceneTexture>  textures;
    std::vector<uint32_t>      image_argb;    // so com image
    std::vector<Probe>         probes;
    std::string                blocked;       // nao vazio: sem valor esperado ainda (motivo)
};

std::vector<FidelityScene> fidelity_scenes();

// Mistura de um canal como o PVR (fator alfa/255), arredondada; exposta para os testes
uint32_t blend_rgb(uint32_t src_rgb, uint32_t dst_rgb, uint32_t alpha);

} // namespace bs::native::render
