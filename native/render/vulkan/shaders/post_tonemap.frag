#version 450
// N7c: tonemapping interno (cadeia de pos-processamento, render/post_shader.h). A imagem do jogo e tratada como sRGB
// aproximado (gamma 2,2): exposicao em EV na luz linear, compressao das altas luzes por um joelho suave (abaixo do
// joelho nada muda; acima, a luz que cortaria em 1 vai sendo comprimida ate o branco, sem alcanca-lo) e ajuste das
// sombras.
// p[0].x exposicao (-2 a 2 EV), p[0].y compressao das altas luzes (0 a 1), p[0].z sombras (-0,5 a 0,5).
// Neutro (0, 0, 0): a mesma imagem.
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Post {
    vec4 size;
    vec4 p[4];
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 c   = texture(prev, vUv).rgb;
    vec3 lin = pow(c, vec3(2.2)) * exp2(pc.p[0].x);
    // altas luzes: sem compressao (0), o que passa de 1 corta; com ela, o joelho desce de 1 ate 0,4 e o excesso vira
    // k + (1 - k)(1 - e^(-(x - k)/(1 - k))), continuo e com a mesma inclinacao no joelho
    const float k = 1.0 - 0.6 * clamp(pc.p[0].y, 0.0, 1.0);
    if (k < 1.0) {
        const vec3 comp = k + (1.0 - k) * (1.0 - exp(-max(lin - k, 0.0) / (1.0 - k)));
        lin             = mix(lin, comp, step(vec3(k), lin));
    }
    lin = min(lin, vec3(1.0));
    vec3 g          = pow(lin, vec3(1.0 / 2.2));
    // sombras: levanta (positivo) ou aprofunda (negativo) so a parte escura
    g        = g + pc.p[0].z * (1.0 - smoothstep(0.0, 0.5, g)) * 0.5;
    outColor = vec4(clamp(g, 0.0, 1.0), 1.0);
}
