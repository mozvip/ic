#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;
layout(set = 3, binding = 0) uniform Params {
    vec2  image_size;
    float time;
} u;

void main() {
    float block = max(4.0, min(24.0, u.image_size.x / 200.0));
    vec2 block_uv = (floor(v_uv * u.image_size / block) + 0.5) * block / u.image_size;
    vec4 c = texture(u_tex, block_uv);
    frag_color = c;
}
