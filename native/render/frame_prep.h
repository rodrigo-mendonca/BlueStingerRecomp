#pragma once
// N7b: preparacao portavel de um quadro da IR para um backend de GPU (sem API grafica). O que o rasterizador de
// referencia (runtime/pvr/pvr_render.cpp, Raster::draw) decide por triangulo fica aqui, na CPU, com as mesmas
// contas: area com sinal, culling (pequeno, anti-horario, horario), descarte de area nula ou nao finita, recorte
// "dentro" (retangulo de tesoura) e "fora" (o shader descarta), cor do ultimo vertice no sombreamento plano.
// Os strips viram triangulos na ordem do decodificador do TA (k impar: os dois primeiros trocados).
//
// Ordem das fases, como o software: lista opaca, punch-through, translucida; os volumes modificadores ficam na IR
// (o software ainda nao os aplica). `seq` numera os triangulos na ordem de desenho (desempate do autosort).
// Profundidade: o proprio 1/w vezes `depth_scale`, uma potencia de 2 que leva o maior 1/w do quadro (e o do fundo)
// para <= 1: a escala e exata, as comparacoes (inclusive a igualdade) nao mudam.
#include "render/render.h"

#include <cstdint>
#include <vector>

namespace bs::native::render {

struct GpuVertex {  // 36 bytes, o layout do pvr.vert
    float    x = 0, y = 0, z = 0, u = 0, v = 0;
    uint32_t color = 0, offset = 0, seq = 0;
    uint32_t tri = 0;  // indice do triangulo em PreparedFrame::tri_data (modo fiel)
};

// Palavras por triangulo em PreparedFrame::tri_data (o layout de pvr.frag): ax ay bx by cx cy | az bz cz | 1/area |
// ua va ub vb uc vc | cor a b c | deslocamento a b c | 2 de folga. Posicoes ORIGINAIS (sem o meio pixel de
// HALF_OFFSET) e 1/area calculado aqui como o software (1.f / area em float).
inline constexpr uint32_t kTriWords = 24;

enum class PassMode : uint8_t { Opaque = 0, PunchThrough = 1, TranslucentOrdered = 2, TranslucentAutosort = 3 };

struct Batch {
    PassMode  mode = PassMode::Opaque;
    DrawState state;
    uint32_t  first = 0, count = 0;  // vertices (3 por triangulo; 6 no modo fiel, o retangulo)
    bool      scissor = false;       // recorte "dentro": so o retangulo do estado
};

struct PreparedFrame {
    std::vector<GpuVertex> vertices;
    std::vector<Batch>     batches;
    // Modo fiel: os dados de cada triangulo (kTriWords palavras) para o shader repetir as contas do software
    // (cobertura, 1/w e atributos) em vez da interpolacao da GPU, que encaixa os vertices em 1/256 de pixel.
    std::vector<uint32_t>  tri_data;
    float                  depth_scale = 1;
    // Com 1/w NEGATIVO no quadro (o jogo envia, raramente: 1 render em 4487 na rota de combate), a profundidade e
    // (1/w - depth_bias) * depth_scale, com depth_bias = o menor 1/w: a mesma funcao monotona para os vertices e o
    // fundo, de modo que nada vira 0 pelo clamp; a subtracao pode juntar valores muito proximos (contado no backend).
    // Sem 1/w negativo, depth_bias = 0 e a escala continua exata.
    float                  depth_bias = 0;
    uint32_t               triangles = 0;           // enviados
    uint32_t               culled = 0;              // culling e area nula/nao finita (como o software)
    uint32_t               nonfinite_depth = 0;     // 1/w NaN ou infinito: descartados (o software nao os desenha)
    bool                   has_autosort = false;
};

// exact = modo fiel: cada triangulo vira o RETANGULO envolvente em pixels inteiros (o mesmo laco do software:
// x de floor(min) a ceil(max), limitado ao quadro e ao recorte "dentro") e o shader decide a cobertura com as contas
// do software, independente da rasterizacao da GPU. Sem exact: os proprios triangulos (melhorias do N7c).
void prepare_frame(const CommandList& cl, PreparedFrame& out, bool exact = false);

} // namespace bs::native::render
