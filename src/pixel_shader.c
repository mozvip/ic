/* pixel_shader.c - CPU-side pixel shader implementations */

#include "pixel_shader.h"
#include "comic_viewer.h"
#include "gpu_shader.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Generic helpers                                                     */
/* ------------------------------------------------------------------ */

static inline Uint8 clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (Uint8)v;
}

static Uint32 get_pixel32(const SDL_Surface *surface, int x, int y) {
    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(surface->format);
    int bpp = fmt->bytes_per_pixel;
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
    switch (bpp) {
        case 1: return *p;
        case 2: return *(Uint16 *)p;
        case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            return ((Uint32)p[0] << 16) | ((Uint32)p[1] << 8) | p[2];
#else
            return p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16);
#endif
        case 4: return *(Uint32 *)p;
    }
    return 0;
}

static void put_pixel32(SDL_Surface *surface, int x, int y, Uint32 pixel) {
    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(surface->format);
    int bpp = fmt->bytes_per_pixel;
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
    switch (bpp) {
        case 1: *p = (Uint8)pixel; break;
        case 2: *(Uint16 *)p = (Uint16)pixel; break;
        case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
#else
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
#endif
            break;
        case 4: *(Uint32 *)p = pixel; break;
    }
}

/* Walk every pixel of a surface and apply a per-pixel RGBA transform. */
typedef void (*RgbaTransform)(int x, int y, int w, int h, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a);

static void transform_surface(SDL_Surface *surface, RgbaTransform fn) {
    if (!surface || !fn) return;
    if (!SDL_LockSurface(surface)) return;

    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(surface->format);
    SDL_Palette *palette = SDL_GetSurfacePalette(surface);
    int w = surface->w, h = surface->h;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Uint32 pixel = get_pixel32(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);
            fn(x, y, w, h, &r, &g, &b, &a);
            put_pixel32(surface, x, y, SDL_MapRGBA(fmt, palette, r, g, b, a));
        }
    }

    SDL_UnlockSurface(surface);
}

/* ------------------------------------------------------------------ */
/* Per-pixel kernels (used by both surface filter and overlay)         */
/* Each takes (x, y, w, h) and a starting RGBA and transforms in place.*/
/* ------------------------------------------------------------------ */

static void kernel_scanlines(int x, int y, int w, int h, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) {
    (void)x; (void)w; (void)h; (void)a;
    /* Dim every other line; also a slight RGB phosphor mask per column. */
    if ((y & 1) == 0) {
        *r = (Uint8)(*r * 60 / 100);
        *g = (Uint8)(*g * 60 / 100);
        *b = (Uint8)(*b * 60 / 100);
    }
}

static void kernel_vignette(int x, int y, int w, int h, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) {
    (void)a;
    if (w <= 0 || h <= 0) return;
    float nx = ((float)x / (float)w) * 2.0f - 1.0f;
    float ny = ((float)y / (float)h) * 2.0f - 1.0f;
    float d2 = nx * nx + ny * ny;       /* 0 at center, ~2 at corners */
    float falloff = 1.0f - d2 * 0.55f;  /* tweakable strength */
    if (falloff < 0.0f) falloff = 0.0f;
    *r = clamp_u8((int)(*r * falloff));
    *g = clamp_u8((int)(*g * falloff));
    *b = clamp_u8((int)(*b * falloff));
}

static void kernel_sepia(int x, int y, int w, int h, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) {
    (void)x; (void)y; (void)w; (void)h; (void)a;
    int R = *r, G = *g, B = *b;
    int nr = (int)(R * 0.393 + G * 0.769 + B * 0.189);
    int ng = (int)(R * 0.349 + G * 0.686 + B * 0.168);
    int nb = (int)(R * 0.272 + G * 0.534 + B * 0.131);
    *r = clamp_u8(nr);
    *g = clamp_u8(ng);
    *b = clamp_u8(nb);
}

static void kernel_invert(int x, int y, int w, int h, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) {
    (void)x; (void)y; (void)w; (void)h; (void)a;
    *r = (Uint8)(255 - *r);
    *g = (Uint8)(255 - *g);
    *b = (Uint8)(255 - *b);
}

/* ------------------------------------------------------------------ */
/* Surface-filter implementations                                      */
/* ------------------------------------------------------------------ */

static void surface_scanlines(SDL_Surface *s) { transform_surface(s, kernel_scanlines); }
static void surface_vignette(SDL_Surface *s)  { transform_surface(s, kernel_vignette);  }
static void surface_sepia(SDL_Surface *s)     { transform_surface(s, kernel_sepia);     }
static void surface_invert(SDL_Surface *s)    { transform_surface(s, kernel_invert);    }

static void surface_pixelate(SDL_Surface *surface) {
    if (!surface) return;
    if (!SDL_LockSurface(surface)) return;

    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(surface->format);
    SDL_Palette *palette = SDL_GetSurfacePalette(surface);
    int w = surface->w, h = surface->h;

    /* Block size scaled to image size, capped so it stays visible. */
    int block = w > 0 ? w / 200 : 0;
    if (block < 4) block = 4;
    if (block > 24) block = 24;

    for (int by = 0; by < h; by += block) {
        for (int bx = 0; bx < w; bx += block) {
            int x2 = bx + block; if (x2 > w) x2 = w;
            int y2 = by + block; if (y2 > h) y2 = h;
            long sr = 0, sg = 0, sb = 0, sa = 0, n = 0;
            for (int y = by; y < y2; y++) {
                for (int x = bx; x < x2; x++) {
                    Uint32 p = get_pixel32(surface, x, y);
                    Uint8 r, g, b, a;
                    SDL_GetRGBA(p, fmt, palette, &r, &g, &b, &a);
                    sr += r; sg += g; sb += b; sa += a; n++;
                }
            }
            if (n == 0) continue;
            Uint8 r = (Uint8)(sr / n), g = (Uint8)(sg / n);
            Uint8 b = (Uint8)(sb / n), a = (Uint8)(sa / n);
            Uint32 packed = SDL_MapRGBA(fmt, palette, r, g, b, a);
            for (int y = by; y < y2; y++) {
                for (int x = bx; x < x2; x++) {
                    put_pixel32(surface, x, y, packed);
                }
            }
        }
    }

    SDL_UnlockSurface(surface);
}

/* Mirror as a surface filter mirrors horizontally over its own self.
 * Not particularly useful as a filter, but provided for symmetry. */
static void surface_mirror(SDL_Surface *surface) {
    if (!surface) return;
    if (!SDL_LockSurface(surface)) return;
    int w = surface->w, h = surface->h;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w / 2; x++) {
            Uint32 a = get_pixel32(surface, x, y);
            Uint32 b = get_pixel32(surface, w - 1 - x, y);
            put_pixel32(surface, x, y, b);
            put_pixel32(surface, w - 1 - x, y, a);
        }
    }
    SDL_UnlockSurface(surface);
}

/* ------------------------------------------------------------------ */
/* Overlay rendering                                                   */
/* ------------------------------------------------------------------ */

/* Sample a pixel from view->surface, with clamping. Returns 0 on failure. */
static Uint32 sample_view(SDL_Surface *src, int x, int y) {
    if (!src) return 0;
    if (x < 0) x = 0; else if (x >= src->w) x = src->w - 1;
    if (y < 0) y = 0; else if (y >= src->h) y = src->h - 1;
    return get_pixel32(src, x, y);
}

/* Fill a 32-bit RGBA8888 streaming texture by invoking `paint` per pixel. */
typedef void (*OverlayPainter)(int x, int y, int w, int h,
                               Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a,
                               ImageView *view, bool right_side);

static void render_painter_to_rect(SDL_Renderer *renderer,
                                   const SDL_FRect *dst,
                                   ImageView *view,
                                   bool right_side,
                                   OverlayPainter paint) {
    if (!renderer || !dst || dst->w <= 0.0f || dst->h <= 0.0f) return;

    /* Render to a downscaled buffer (then stretched), keeping the cost low. */
    int tw = (int)dst->w / 2; if (tw < 32) tw = (int)dst->w; if (tw < 1) tw = 1;
    int th = (int)dst->h / 2; if (th < 32) th = (int)dst->h; if (th < 1) th = 1;

    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING, tw, th);
    if (!tex) return;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

    void *pixels = NULL;
    int pitch = 0;
    if (!SDL_LockTexture(tex, NULL, &pixels, &pitch)) {
        SDL_DestroyTexture(tex);
        return;
    }

    for (int y = 0; y < th; y++) {
        Uint8 *row = (Uint8 *)pixels + y * pitch;
        for (int x = 0; x < tw; x++) {
            Uint8 r = 0, g = 0, b = 0, a = 255;
            paint(x, y, tw, th, &r, &g, &b, &a, view, right_side);
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = a;
        }
    }
    SDL_UnlockTexture(tex);

    SDL_RenderTexture(renderer, tex, NULL, dst);
    SDL_DestroyTexture(tex);
}

/* ----- per-shader overlay painters --------------------------------- */

static void paint_scanlines(int x, int y, int w, int h,
                            Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a,
                            ImageView *view, bool right_side) {
    (void)x; (void)w; (void)view; (void)right_side;
    /* Dim, scanlined dark backdrop. */
    Uint8 base = 18;
    if ((y * 4 / h) & 1) base = 28;
    *r = base; *g = base; *b = base; *a = 255;
    if ((y & 1) == 0) {
        *r = (Uint8)(*r / 2);
        *g = (Uint8)(*g / 2);
        *b = (Uint8)(*b / 2);
    }
}

static void paint_vignette(int x, int y, int w, int h,
                           Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a,
                           ImageView *view, bool right_side) {
    (void)view;
    /* Darker farther from the image edge. */
    float t;
    if (right_side) t = (float)x / (float)(w - 1 > 0 ? w - 1 : 1);
    else            t = 1.0f - (float)x / (float)(w - 1 > 0 ? w - 1 : 1);
    float vy = ((float)y / (float)h) * 2.0f - 1.0f;
    float dim = (1.0f - t) * (1.0f - 0.4f * vy * vy);
    if (dim < 0.0f) dim = 0.0f;
    Uint8 v = (Uint8)(40.0f * dim);
    *r = v; *g = v; *b = v; *a = 255;
}

static void paint_sepia(int x, int y, int w, int h,
                        Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a,
                        ImageView *view, bool right_side) {
    (void)x; (void)y; (void)w; (void)h; (void)view; (void)right_side;
    *r = 112; *g = 66; *b = 20; *a = 255;
    kernel_sepia(0, 0, 1, 1, r, g, b, a);
    /* Darken slightly so it doesn't compete with the image. */
    *r = (Uint8)(*r / 2);
    *g = (Uint8)(*g / 2);
    *b = (Uint8)(*b / 2);
}

static void paint_invert(int x, int y, int w, int h,
                         Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a,
                         ImageView *view, bool right_side) {
    (void)x; (void)y; (void)w; (void)h; (void)view; (void)right_side;
    /* Flat near-white frame, the visual inverse of a black backdrop. */
    *r = 220; *g = 220; *b = 220; *a = 255;
}

static void paint_pixelate(int x, int y, int w, int h,
                           Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a,
                           ImageView *view, bool right_side) {
    (void)w;
    SDL_Surface *src = view ? view->surface : NULL;
    if (!src) { *r = 30; *g = 30; *b = 30; *a = 255; return; }

    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(src->format);
    SDL_Palette *palette = SDL_GetSurfacePalette(src);

    /* Sample a vertical strip at the image's matching edge. */
    int strip_w = 16;
    int sx;
    if (right_side) {
        sx = (int)(view->crop_rect.x + view->crop_rect.w - 1 - (x % strip_w));
    } else {
        sx = (int)(view->crop_rect.x + (x % strip_w));
    }
    int sy = (int)(view->crop_rect.y + ((float)y / (float)h) * view->crop_rect.h);

    /* Block-snap to ~16px blocks on source. */
    int blk = 16;
    sx = (sx / blk) * blk;
    sy = (sy / blk) * blk;

    Uint32 p = sample_view(src, sx, sy);
    Uint8 sr, sg, sb, sa;
    SDL_GetRGBA(p, fmt, palette, &sr, &sg, &sb, &sa);
    *r = sr; *g = sg; *b = sb; *a = 200;
}

static void paint_mirror(int x, int y, int w, int h,
                         Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a,
                         ImageView *view, bool right_side) {
    SDL_Surface *src = view ? view->surface : NULL;
    if (!src) { *r = 30; *g = 30; *b = 30; *a = 255; return; }

    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(src->format);
    SDL_Palette *palette = SDL_GetSurfacePalette(src);

    /* Mirror the matching edge of the image into the margin, then fade out
     * as we move away from the image. */
    float t = (float)x / (float)(w - 1 > 0 ? w - 1 : 1);    /* 0 at left of dst */
    float dist_from_image = right_side ? t : (1.0f - t);
    int strip_pixels = (int)(view->crop_rect.w * 0.18f);
    if (strip_pixels < 1) strip_pixels = 1;

    int offset = (int)(dist_from_image * strip_pixels);
    int sx;
    if (right_side) sx = (int)(view->crop_rect.x + view->crop_rect.w - 1 - offset);
    else            sx = (int)(view->crop_rect.x + offset);
    int sy = (int)(view->crop_rect.y + ((float)y / (float)h) * view->crop_rect.h);

    Uint32 p = sample_view(src, sx, sy);
    Uint8 sr, sg, sb, sa;
    SDL_GetRGBA(p, fmt, palette, &sr, &sg, &sb, &sa);

    /* Fade towards black at the far edge. */
    float fade = 1.0f - dist_from_image;
    if (fade < 0.0f) fade = 0.0f;
    *r = clamp_u8((int)(sr * fade));
    *g = clamp_u8((int)(sg * fade));
    *b = clamp_u8((int)(sb * fade));
    *a = 220;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int pixel_shader_count(void) {
    return (int)PIXEL_SHADER_COUNT;
}

const char *pixel_shader_get_name(PixelShaderType type) {
    switch (type) {
        case PIXEL_SHADER_NONE:      return "None";
        case PIXEL_SHADER_SCANLINES: return "Scanlines";
        case PIXEL_SHADER_VIGNETTE:  return "Vignette";
        case PIXEL_SHADER_PIXELATE:  return "Pixelate";
        case PIXEL_SHADER_SEPIA:     return "Sepia";
        case PIXEL_SHADER_INVERT:    return "Invert";
        case PIXEL_SHADER_MIRROR:    return "Mirror";
        default:                     return "Unknown";
    }
}

void pixel_shader_apply_to_surface(PixelShaderType type, SDL_Surface *surface) {
    if (!surface) return;
    if (type == PIXEL_SHADER_NONE) return;

    /* Prefer the SDL_GPU implementation when available. Fall back to the CPU
     * version on any failure so a missing GPU shader doesn't break the view. */
    if (gpu_shader_has(type) && gpu_shader_apply_to_surface(type, surface)) {
        return;
    }

    switch (type) {
        case PIXEL_SHADER_NONE:                                return;
        case PIXEL_SHADER_SCANLINES: surface_scanlines(surface); return;
        case PIXEL_SHADER_VIGNETTE:  surface_vignette(surface);  return;
        case PIXEL_SHADER_PIXELATE:  surface_pixelate(surface);  return;
        case PIXEL_SHADER_SEPIA:     surface_sepia(surface);     return;
        case PIXEL_SHADER_INVERT:    surface_invert(surface);    return;
        case PIXEL_SHADER_MIRROR:    surface_mirror(surface);    return;
        default: return;
    }
}

void pixel_shader_render_overlay(PixelShaderType type,
                                 SDL_Renderer *renderer,
                                 const SDL_FRect *dst,
                                 struct ImageView *view,
                                 bool right_side,
                                 float display_width,
                                 float display_height) {
    (void)display_width; (void)display_height;
    if (!renderer || !dst || dst->w <= 0.0f || dst->h <= 0.0f) return;

    OverlayPainter paint = NULL;
    switch (type) {
        case PIXEL_SHADER_NONE:                          return;
        case PIXEL_SHADER_SCANLINES: paint = paint_scanlines; break;
        case PIXEL_SHADER_VIGNETTE:  paint = paint_vignette;  break;
        case PIXEL_SHADER_PIXELATE:  paint = paint_pixelate;  break;
        case PIXEL_SHADER_SEPIA:     paint = paint_sepia;     break;
        case PIXEL_SHADER_INVERT:    paint = paint_invert;    break;
        case PIXEL_SHADER_MIRROR:    paint = paint_mirror;    break;
        default: return;
    }
    render_painter_to_rect(renderer, dst, view, right_side, paint);
}
