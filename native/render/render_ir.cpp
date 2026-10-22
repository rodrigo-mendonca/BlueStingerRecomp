#include "render/render_ir.h"

#include <algorithm>

namespace bs::native::render {

namespace {
bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}
int rank(ListType t) {  // a ordem em que o PVR2 resolve as listas
    switch (t) {
    case ListType::Opaque:
    case ListType::OpaqueModifier: return 0;
    case ListType::PunchThrough: return 1;
    case ListType::Translucent:
    case ListType::TranslucentModifier: return 2;
    }
    return 3;
}
} // namespace

bool validate(const CommandList& cl, std::string* err) {
    if (cl.frame.width == 0 || cl.frame.height == 0 || cl.frame.width > 4096 || cl.frame.height > 4096)
        return fail(err, "quadro com dimensoes invalidas");
    const size_t nv = cl.vertices.size();
    for (size_t i = 0; i < cl.draws.size(); ++i) {
        const Draw& d = cl.draws[i];
        if (d.vertex_count < 3) return fail(err, "desenho " + std::to_string(i) + ": strip com menos de 3 vertices");
        if (size_t(d.first_vertex) + d.vertex_count > nv)
            return fail(err, "desenho " + std::to_string(i) + ": vertices fora do vetor");
        if (rank(d.state.list) > 2) return fail(err, "desenho " + std::to_string(i) + ": lista desconhecida");
    }
    for (size_t i = 0; i < cl.modifiers.size(); ++i) {
        const ModifierVolume& m = cl.modifiers[i];
        if (size_t(m.first_vertex) + size_t(m.triangle_count) * 3 > nv)
            return fail(err, "volume " + std::to_string(i) + ": vertices fora do vetor");
        if (m.list != ListType::OpaqueModifier && m.list != ListType::TranslucentModifier)
            return fail(err, "volume " + std::to_string(i) + ": lista nao e de modificador");
    }
    // Os valores dos vertices sao dados do jogo e nao invalidam a IR: o jogo envia, raramente, vertice com NaN ou
    // 1/w negativo (1 em 4 487 renders na rota de combate, N7a); o backend segue a regra do rasterizador de referencia
    // (triangulo com area nao finita e descartado; o resto passa pelos testes de profundidade como chegou).
    return true;
}

std::vector<uint32_t> draw_order(const CommandList& cl) {
    std::vector<uint32_t> idx(cl.draws.size());
    for (uint32_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](uint32_t a, uint32_t b) { return rank(cl.draws[a].state.list) < rank(cl.draws[b].state.list); });
    return idx;
}

std::vector<uint32_t> modifier_order(const CommandList& cl, ListType list) {
    std::vector<uint32_t> idx;
    for (uint32_t i = 0; i < cl.modifiers.size(); ++i)
        if (cl.modifiers[i].list == list) idx.push_back(i);
    return idx;
}

} // namespace bs::native::render
