#version 450
// teste do validador (render/post_shader.h): um passe que escreve num buffer de armazenamento tem de ser recusado
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(std430, set = 0, binding = 2) buffer Saida { uint vazamento[]; };
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() {
    vazamento[0] = 1u;
    outColor     = texture(prev, vUv);
}
