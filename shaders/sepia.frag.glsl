#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;

void main() {
    vec4 c = texture(u_tex, v_uv);
    vec3 s = vec3(
        dot(c.rgb, vec3(0.393, 0.769, 0.189)),
        dot(c.rgb, vec3(0.349, 0.686, 0.168)),
        dot(c.rgb, vec3(0.272, 0.534, 0.131))
    );
    frag_color = vec4(clamp(s, 0.0, 1.0), c.a);
}
