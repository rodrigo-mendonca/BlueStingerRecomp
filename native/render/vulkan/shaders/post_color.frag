#version 450
// N7c: correcao de cor interna (o primeiro efeito da cadeia de pos-processamento). A interface e a de qualquer passe
// (render/post_shader.h): p[0].x brilho (-0,5 a 0,5), p[0].y contraste (0,5 a 1,5), p[0].z gamma (0,5 a 2,5),
// p[0].w saturacao (0 a 2). Neutro (0, 1, 1, 1): a mesma imagem.
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Post {
    vec4 size;  // largura, altura, 1/largura, 1/altura
    vec4 p[4];
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 c = texture(prev, vUv).rgb;
    c      = (c - 0.5) * pc.p[0].y + 0.5 + pc.p[0].x;
    c      = pow(clamp(c, 0.0, 1.0), vec3(1.0 / pc.p[0].z));
    const float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c      = mix(vec3(l), c, pc.p[0].w);
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
