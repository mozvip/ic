#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;

void main() {
    vec4 c = texture(u_tex, v_uv);
    vec2 d = v_uv * 2.0 - 1.0;
    float fall = 1.0 - dot(d, d) * 0.55;
    fall = clamp(fall, 0.0, 1.0);
    frag_color = vec4(c.rgb * fall, c.a);
}
