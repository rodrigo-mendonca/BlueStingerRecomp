#version 450
// N7c: passe de pos-processamento (docs/N7-PLANO.md secao 21): um triangulo que cobre a imagem inteira, sem buffer;
// uv de 0 a 1 em location 0 (a interface dos shaders de passe, render/post_shader.h).
layout(location = 0) out vec2 vUv;

void main() {
    const vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUv          = p;
    gl_Position  = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
