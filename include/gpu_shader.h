/* gpu_shader.h - SDL_GPU fragment-shader pixel filter
 *
 * Runs an SPIR-V fragment shader over an SDL_Surface using SDL_GPU. The
 * surface is uploaded as a sampled texture, the shader writes to an offscreen
 * render target, and the result is read back into the surface. Used as the
 * GPU implementation of the pixel-shader view filter; the CPU implementation
 * in pixel_shader.c is the fallback when the GPU path is unavailable or a
 * particular shader has no GPU variant.
 */

#ifndef GPU_SHADER_H
#define GPU_SHADER_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "pixel_shader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the GPU shader subsystem.
 *
 * Tries to reuse the SDL_GPUDevice that backs `renderer` (when the renderer's
 * driver is "gpu"); otherwise creates its own. Returns true on success.
 * Compiling pipelines for the built-in shaders is also done here.
 */
bool gpu_shader_init(SDL_Renderer *renderer);

void gpu_shader_shutdown(void);

bool gpu_shader_is_available(void);

/* Returns true if a specific shader has a GPU implementation. */
bool gpu_shader_has(PixelShaderType type);

/* Run a shader over `surface` in place. Returns true on success.
 * On failure the surface is left unchanged and callers may fall back to CPU.
 */
bool gpu_shader_apply_to_surface(PixelShaderType type, SDL_Surface *surface);

#ifdef __cplusplus
}
#endif

#endif /* GPU_SHADER_H */
