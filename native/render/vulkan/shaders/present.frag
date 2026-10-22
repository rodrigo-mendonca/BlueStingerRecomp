#version 450
// N7c: apresentacao direta. flags bit 0: imagem do jogo (opaca, o alfa do alvo nao conta); senao, camada
// pre-multiplicada (menu e legendas), misturada pelo pipeline (One, OneMinusSrcAlpha).
layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform Quad {
    vec4 dst;
    vec4 src;
    uint flags;
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    const vec4 c = texture(tex, vUv);
    outColor     = (pc.flags & 1u) != 0u ? vec4(c.rgb, 1.0) : c;
}
