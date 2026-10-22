#version 450
// teste do validador (render/post_shader.h): so sampler2D simples; um sampler3D tem de ser recusado
layout(set = 0, binding = 0) uniform sampler3D prev;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(prev, vec3(vUv, 0.5)); }
