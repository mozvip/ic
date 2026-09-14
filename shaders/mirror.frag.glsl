#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;

void main() {
    frag_color = texture(u_tex, vec2(1.0 - v_uv.x, v_uv.y));
}
