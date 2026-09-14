#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;

void main() {
    vec4 c = texture(u_tex, v_uv);
    frag_color = vec4(1.0 - c.rgb, c.a);
}
