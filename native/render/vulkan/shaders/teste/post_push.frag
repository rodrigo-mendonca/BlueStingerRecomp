#version 450
// teste do validador (render/post_shader.h): push constants acima de 80 bytes tem de ser recusado
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Post { vec4 size; vec4 p[8]; } pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(prev, vUv) * pc.p[7]; }
