/* pixel_shader.h - CPU-side pixel shader API
 *
 * Each pixel shader exposes two entry points:
 *   - apply_to_surface: per-pixel transform applied in place to a view surface
 *                       (used by the "view filter" feature)
 *   - render_overlay:   per-pixel painter that fills an overlay region with the
 *                       shader's look, optionally sampling from the current
 *                       view (used by the OVERLAY_PIXEL_SHADER overlay mode)
 */

#ifndef PIXEL_SHADER_H
#define PIXEL_SHADER_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ImageView;

typedef enum {
    PIXEL_SHADER_NONE = 0,
    PIXEL_SHADER_SCANLINES,
    PIXEL_SHADER_VIGNETTE,
    PIXEL_SHADER_PIXELATE,
    PIXEL_SHADER_SEPIA,
    PIXEL_SHADER_INVERT,
    PIXEL_SHADER_MIRROR,
    PIXEL_SHADER_COUNT
} PixelShaderType;

int pixel_shader_count(void);
const char *pixel_shader_get_name(PixelShaderType type);

/* Apply a shader to a surface in place. NONE is a no-op. */
void pixel_shader_apply_to_surface(PixelShaderType type, SDL_Surface *surface);

/* Render a shader into the given overlay region (left or right margin).
 * `view` is used when the shader samples from the image (e.g. MIRROR).
 * `right_side` indicates whether dst is on the right margin (true) or left (false).
 */
void pixel_shader_render_overlay(PixelShaderType type,
                                 SDL_Renderer *renderer,
                                 const SDL_FRect *dst,
                                 struct ImageView *view,
                                 bool right_side,
                                 float display_width,
                                 float display_height);

#ifdef __cplusplus
}
#endif

#endif /* PIXEL_SHADER_H */
