#pragma once
// N7 (preparacao): regras portaveis sobre a IR de render (render.h), usadas por qualquer backend.
//   validate: a CommandList e coerente (faixas de vertices dentro do vetor, strips com >= 3 vertices,
//             volumes dentro do vetor, dimensoes do quadro); erro com o indice do desenho culpado. So a
//             estrutura: os valores dos vertices (NaN, 1/w negativo) sao dados do jogo (o backend segue a referencia).
//   draw_order: a ordem do PVR2 - lista opaca (e os modificadores opacos), punch-through, translucida
//             (e os modificadores translucidos); dentro de cada lista, a ordem de submissao. Sem
//             ordenacao por profundidade aqui: o jogo liga o autosort (ISP_FEED_CFG = 0x00800408, STATUS.md)
//             e o backend fiel o resolve POR PIXEL (fragmentos por 1/w, sem z-write), como
//             runtime/pvr/pvr_render.cpp; ordenar por triangulo nao e equivalente (docs/N7-PLANO.md, secao 6).
#include "render/render.h"

#include <string>
#include <vector>

namespace bs::native::render {

bool                  validate(const CommandList& cl, std::string* err = nullptr);
std::vector<uint32_t> draw_order(const CommandList& cl);  // indices de cl.draws
// volumes de uma fase (OpaqueModifier: antes da resolucao dos opacos/punch-through; TranslucentModifier:
// antes das translucidas), na ordem de submissao; indices de cl.modifiers
std::vector<uint32_t> modifier_order(const CommandList& cl, ListType list);

} // namespace bs::native::render
