#version 450
// N7c: filtro CRT interno (cadeia de pos-processamento, render/post_shader.h): a apresentacao de uma TV de tubo sobre a
// imagem do jogo. As linhas de varredura seguem as 480 linhas do jogo (perfil gaussiano do feixe em cada linha) e a
// mascara de abertura tem uma triade RGB por coluna do jogo (640): precisa de varias linhas e colunas da imagem por
// linha e coluna do jogo, por isso o menu so o oferece com resolucao interna de 1080p ou mais.
// p[0].x linhas de varredura (0 a 1), p[0].y mascara de abertura (0 a 1), p[0].z vinheta (0 a 1).
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Post {
    vec4 size;
    vec4 p[4];
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 c = texture(prev, vUv).rgb;
    // feixe: mais claro no centro da linha do jogo, escuro entre as linhas (com ganho para manter o brilho medio)
    const float line = fract(vUv.y * 480.0) - 0.5;
    const float beam = exp(-line * line * 18.0);
    c *= mix(1.0, beam * 1.55, pc.p[0].x);
    // mascara: tres faixas (R, G, B) por coluna do jogo
    const int  k    = int(floor(vUv.x * 640.0 * 3.0)) % 3;
    const vec3 tri  = k == 0 ? vec3(1.0, 0.65, 0.65) : (k == 1 ? vec3(0.65, 1.0, 0.65) : vec3(0.65, 0.65, 1.0));
    c *= mix(vec3(1.0), tri * 1.25, pc.p[0].y);
    // vinheta suave
    const vec2 d = vUv - 0.5;
    c *= 1.0 - pc.p[0].z * dot(d, d) * 1.6;
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
