#version 450
// teste do validador (render/post_shader.h): um passe com laco (pode nao terminar) tem de ser recusado
layout(set = 0, binding = 0) uniform sampler2D prev;
layout(push_constant) uniform Post { vec4 size; vec4 p[4]; } pc;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() {
    vec4 c = vec4(0.0);
    for (int i = 0; i < int(pc.p[0].x); ++i) c += texture(prev, vUv + vec2(float(i)) * pc.size.zw);
    outColor = c;
}
