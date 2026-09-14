#version 460

layout(location = 0) out vec2 v_uv;

void main() {
    // Fullscreen triangle: vertex indices 0, 1, 2 cover the screen.
    // Flip the sampled-texture Y so that source-surface row 0 (top) ends up at
    // the top of the SDL_GPU framebuffer. Without this the rendered output is
    // vertically mirrored because the GLSL clip-space convention disagrees
    // with the SDL_GPU framebuffer row order.
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_uv = vec2(uv.x, 1.0 - uv.y);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
