#version 450
// N7c: bloom interno, passe 1 de 2 (cadeia de pos-processamento, render/post_shader.h): so o que passa do limiar
// (limiar suave pelo canal mais forte) e borrado na horizontal, gaussiano de 9 amostras. O raio e uma fracao da altura da
// imagem: o mesmo brilho em qualquer resolucao interna.
// p[0].x limiar (0 a 1), p[0].y raio (fracao da altura, 0,005 a 0,05).
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Post {
    vec4 size;
    vec4 p[4];
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

const float kW[5] = float[](0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);

vec3 bright(vec2 uv) {
    const vec3  c = texture(prev, uv).rgb;
    const float l = max(c.r, max(c.g, c.b));  // o canal mais forte: luzes coloridas (o azul do jogo) tambem brilham
    return c * (max(l - pc.p[0].x, 0.0) / max(l, 1e-4));
}

void main() {
    const float step = pc.p[0].y * pc.size.y / 4.0 * pc.size.z;  // 4 amostras = o raio, em uv
    vec3        s    = bright(vUv) * kW[0];
    for (int i = 1; i < 5; ++i)
        s += (bright(vUv + vec2(step * float(i), 0.0)) + bright(vUv - vec2(step * float(i), 0.0))) * kW[i];
    outColor = vec4(s, 1.0);
}
