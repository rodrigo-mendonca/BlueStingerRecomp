#version 450
// Exemplo de shader de pos-processamento de mod (docs/N7-PLANO.md secao 21): "cor quente". Muda a temperatura e o
// matiz da imagem do jogo e escurece as bordas. Interface dos passes: render/post_shader.h.
// Compilar: glslangValidator -V --target-env vulkan1.1 shader.frag -o shader.spv
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Post {
    vec4 size;  // largura, altura, 1/largura, 1/altura
    vec4 p[4];  // p[0].x temperatura, p[0].y matiz, p[0].z vinheta
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 c = texture(prev, vUv).rgb;
    const float t = pc.p[0].x, m = pc.p[0].y;
    c *= vec3(1.0 + 0.10 * t, 1.0 + 0.05 * m, 1.0 - 0.10 * t);
    const vec2 d = vUv - 0.5;
    c *= 1.0 - pc.p[0].z * dot(d, d) * 2.0;
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
