#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;
layout(set = 3, binding = 0) uniform Params {
    vec2  image_size;
    float time;
} u;

void main() {
    vec4 c = texture(u_tex, v_uv);
    int y = int(v_uv.y * u.image_size.y);
    if ((y & 1) == 0) c.rgb *= 0.6;
    frag_color = c;
}
