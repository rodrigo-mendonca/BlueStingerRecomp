#version 450
// N7c: bloom interno, passe 2 de 2: borra na vertical o resultado do passe 1 e soma a imagem de antes do bloom
// (binding 1, a entrada do passe 1).
// p[0].y raio (o mesmo do passe 1), p[0].z intensidade (0 a 2).
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(set = 0, binding = 1) uniform sampler2D before;
layout(push_constant) uniform Post {
    vec4 size;
    vec4 p[4];
} pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

const float kW[5] = float[](0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);

void main() {
    const float step = pc.p[0].y * pc.size.y / 4.0 * pc.size.w;
    vec3        s    = texture(prev, vUv).rgb * kW[0];
    for (int i = 1; i < 5; ++i)
        s += (texture(prev, vUv + vec2(0.0, step * float(i))).rgb + texture(prev, vUv - vec2(0.0, step * float(i))).rgb) * kW[i];
    outColor = vec4(clamp(texture(before, vUv).rgb + s * pc.p[0].z, 0.0, 1.0), 1.0);
}
