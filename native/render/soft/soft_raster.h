#pragma once
// N8.7: o rasterizador de referencia do PowerVR2 em software, sem o hardware simulado. E o recuo permanente do Vulkan e
// a referencia das comparacoes. As contas sao as do rasterizador do N1 (antes em runtime/pvr/pvr_render.cpp, movidas sem
// mudanca): buffer de 32 bits e profundidade em 1/w; listas opaca, punch-through e translucida (autosort por pixel ou na
// ordem de submissao); interpolacao com correcao de perspectiva, regra top-left, amostras por HALF_OFFSET; recorte do
// usuario; color clamp. Os triangulos chegam com as palavras do hardware (ISP/TSP/TCW) e os texels vem de uma
// TexelSource: a VRAM no caminho do TA (validacao) ou as texturas do dispositivo (a IR, soft_device.h).
// N8.7: fog por tabela: o alfa da tabela pela densidade x 1/w do pixel (16 entradas por oitava, os dois valores
// da entrada interpolados pela fracao) mistura a cor final com FOG_COL_RAM, depois da textura, do offset e do clamp.
// N8.7: volumes modificadores da lista 1 com a sombra por intensidade (FPU_SHAD_SCALE), com a semantica de referencia
// do Flycast (so comportamento): paridade a frente da profundidade, fechamento por inclusao/exclusao, escala da cor.
// Aproximacoes (STATUS.md): sem fog por vertice e Table2 (o jogo nao usa), dither, mipmap, VQ, YUV, bump, dois volumes, volumes da lista 3, buffers
// secundarios (SRC/DST select) e escala do SCALER_CTL.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace bs::native::render::soft {

struct Color {
    float a = 0, r = 0, g = 0, b = 0;  // [0, 1], ja quantizados como o TA guarda (8 bits por canal)
};

struct Vertex {
    float x = 0, y = 0, z = 0, u = 0, v = 0;  // z = 1/w
    Color col, off;
};

// Um triangulo com o estado decodificado do global e do recorte do usuario (as palavras do hardware, sem traducao).
struct Tri {
    uint32_t list = 0, isp = 0, tsp = 0, tcw = 0;
    bool     tex = false, offset = false, gouraud = true;
    bool     shadow = false;                     // PCW bit 7: o poligono e afetado pelos volumes modificadores
    uint32_t clipMode = 0;                       // PCW bits 17-16: 2 dentro, 3 fora do recorte
    int      cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;  // recorte do usuario em pixels (inclusivo)
    uint32_t texture = 0;                        // textura do dispositivo (a IR; 0 no caminho do TA, que usa o TCW)
    Vertex   v[3];
};

// Os registradores do quadro que o rasterizador usa (lidos uma vez por render): fundo, profundidade do fundo,
// FPU_CULL_VAL, stride linear das texturas (TEXT_CONTROL), PT_ALPHA_REF, HALF_OFFSET, clamp de cor e a ordenacao dos
// translucidos.
struct Frame {
    uint32_t background = 0;  // RGB do plano de fundo
    float    background_depth = 0, cull_val = 0;
    uint32_t tex_stride = 0, pt_ref = 0, half_offset = 0, clamp_max = 0, clamp_min = 0;
    bool     autosort = true;
    uint32_t shadow_scale = 0;  // N8.7: FPU_SHAD_SCALE (bit 8: sombra por intensidade; bits 7-0: a escala, / 256)
    // N8.7: fog por tabela (TSP bits 23-22 = 0). FOG_TABLE (128 entradas, 2 valores de 8 bits), FOG_COL_RAM (RGB) e
    // FOG_DENSITY (bits 15-8 mantissa / 128, bits 7-0 expoente com sinal)
    std::array<uint16_t, 128> fog_table{};
    uint32_t                  fog_color = 0, fog_density = 0;
    bool                      fog_disabled = false;  // diagnostico (fog_volumes_disabled)
    bool     operator==(const Frame&) const = default;
};

// N8.7: um volume modificador (as listas 1 e 3 do TA): triangulos de x, y e 1/w, na ordem de submissao. Cada triangulo
// que fica a frente da profundidade das opacas (1/w maior) inverte a paridade do pixel; a partir de or_from (o parametro
// de fechamento sem o bit "volume" do PCW: volume aberto ou quad) ele a liga (OU). Fechado (instrucao 1 ou 2), a area do
// volume entra no resultado: inclusao por OU, exclusao por E. O descarte de faces (cull) vale para os triangulos do
// volume, como o do ISP.
struct Volume {
    uint32_t first = 0, count = 0;  // triangulos em volume_tris
    uint32_t list = 1;              // 1 sobre as opacas e o punch-through, 3 sobre as translucidas
    bool     inclusion = true, closed = false;
    uint8_t  cull = 0;
    uint32_t or_from = ~0u;
};
struct VolumeTri {
    float x[3] = {}, y[3] = {}, z[3] = {};
};

// Volumes da lista 3 (sobre as translucidas) nao sao aplicados: contados aqui (o jogo nao os usa nas rotas medidas).
struct RasterInfo {
    uint64_t fragments = 0, shadowed_pixels = 0, translucent_volumes_ignored = 0;
};

// A textura de um desenho: o TCW e o tamanho do TSP; `texture` e a do dispositivo quando vem da IR.
struct TexRef {
    uint32_t tcw = 0, width = 0, height = 0, stride = 0, texture = 0;
};

// De onde vem o texel (x, y ja dentro da textura, depois de wrap/clamp/flip): ARGB8888.
class TexelSource {
public:
    virtual ~TexelSource()                                         = default;
    virtual uint32_t texel(const TexRef& t, uint32_t x, uint32_t y) = 0;
};

struct Image {
    uint32_t              width = 0, height = 0;
    std::vector<uint32_t> color;  // ARGB8888
    std::vector<float>    depth;  // 1/w
};

// Fases medidas por rasterize (BS_RENDER_PROFILE): 1 opaca, 2 punch-through, 3 translucida, 4 resolucao
using PhaseFn = void (*)(void* ctx, int phase);

// Fundo (argb, z) e os triangulos, na ordem das listas (opaca, punch-through, translucida). Devolve o numero de
// fragmentos ordenados por pixel. Roda com o ambiente de ponto flutuante fixo (MXCSR padrao), nesta thread e nos
// workers da resolucao paralela.
size_t rasterize(std::span<const Tri> tris, const Frame& frame, TexelSource& texels, Image& image, PhaseFn phase = nullptr,
                 void* phase_ctx = nullptr);
// N8.7: com os volumes modificadores (sombra por intensidade: FPU_SHAD_SCALE bit 8)
RasterInfo rasterize(std::span<const Tri> tris, std::span<const Volume> volumes, std::span<const VolumeTri> volume_tris,
                     const Frame& frame, TexelSource& texels, Image& image, PhaseFn phase = nullptr, void* phase_ctx = nullptr);

// Diagnostico BS_RENDER_SEM_FOG_VOLUMES=1: o rasterizador e o Vulkan sem a fog por tabela e sem os volumes (N8.7)
bool fog_volumes_disabled();

// Testes: true se, desde a ultima chamada, algum worker da resolucao paralela calculou com MXCSR diferente do padrao.
bool take_fp_violation();

// O ambiente de ponto flutuante do render (MXCSR padrao: mais proximo, sem FTZ/DAZ), restaurado no fim do escopo.
class FpEnv {
public:
    FpEnv();
    ~FpEnv();
    FpEnv(const FpEnv&)            = delete;
    FpEnv& operator=(const FpEnv&) = delete;

private:
    unsigned saved_;
};

} // namespace bs::native::render::soft
