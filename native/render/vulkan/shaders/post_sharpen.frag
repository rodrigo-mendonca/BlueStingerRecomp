#version 450
// N7c: nitidez interna adaptativa ao contraste (cadeia de pos-processamento, render/post_shader.h). Um filtro de realce
// em cruz (centro e os quatro vizinhos) cujo peso negativo cai onde o contraste local ja e alto, para nao criar halos
// nem estourar bordas; formula conhecida de "contrast adaptive sharpening", escrita aqui de forma independente.
// p[0].x intensidade (0 a 1; 0 = a mesma imagem).
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Post {
    vec4 size;  // largura, altura, 1/largura, 1/altura
    vec4 p[4];
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    const vec2 d = pc.size.zw;
    const vec3 c = texture(prev, vUv).rgb;
    const vec3 n = texture(prev, vUv + vec2(0.0, -d.y)).rgb;
    const vec3 s = texture(prev, vUv + vec2(0.0, d.y)).rgb;
    const vec3 w = texture(prev, vUv + vec2(-d.x, 0.0)).rgb;
    const vec3 e = texture(prev, vUv + vec2(d.x, 0.0)).rgb;
    const vec3 mn = min(c, min(min(n, s), min(w, e)));
    const vec3 mx = max(c, max(max(n, s), max(w, e)));
    // quanto de realce a vizinhanca aguenta: perto de 0 onde ja ha muito contraste (ou perto do preto/branco)
    const vec3 room = clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0);
    const vec3 amt  = sqrt(room) * mix(0.0, -0.2, clamp(pc.p[0].x, 0.0, 1.0));  // peso de cada vizinho (negativo)
    const vec3 o    = (c + (n + s + w + e) * amt) / (1.0 + 4.0 * amt);
    outColor        = vec4(clamp(o, 0.0, 1.0), 1.0);
}
