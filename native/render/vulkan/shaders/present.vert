#version 450
// N7c: apresentacao direta. Um retangulo por desenho (6 vertices gerados pelo indice, sem buffer): destino em
// coordenadas normalizadas da saida e o recorte da textura (u0, v0, u1, v1).
layout(push_constant) uniform Quad {
    vec4 dst;  // x0, y0, x1, y1 em [-1, 1]
    vec4 src;  // u0, v0, u1, v1
    uint flags;
} pc;

layout(location = 0) out vec2 vUv;

void main() {
    const vec2 k[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));
    const vec2 t    = k[gl_VertexIndex];
    gl_Position     = vec4(mix(pc.dst.xy, pc.dst.zw, t), 0.0, 1.0);
    vUv             = mix(pc.src.xy, pc.src.zw, t);
}
