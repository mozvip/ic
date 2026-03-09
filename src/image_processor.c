#include "image_processor.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL3/SDL.h>

ImageProcessingOptions* get_default_processing_options(void) {
    ImageProcessingOptions* options = malloc(sizeof(ImageProcessingOptions));
    if (!options) return NULL;

    options->enhancement_enabled = false;
    options->gamma = 1.0;
    options->brightness = 0.0;
    options->contrast = 0.0;
    options->saturation = 1.0;
    options->color_balance = true;
    options->color_filter = COLOR_FILTER_NONE;
    options->sharpen = false;

    return options;
}

const char* color_filter_get_name(ColorFilterType filter_type) {
    switch (filter_type) {
        case COLOR_FILTER_NONE: return "None";
        case COLOR_FILTER_GRAY_WORLD: return "Gray World";
        case COLOR_FILTER_WARM_NEUTRALIZER: return "Reduce Warm";
        case COLOR_FILTER_COOL_NEUTRALIZER: return "Reduce Cool";
        case COLOR_FILTER_BOOST_CONTRAST: return "Boost Contrast";
        case COLOR_FILTER_DESATURATE: return "Desaturate";
        case COLOR_FILTER_BINARIZE: return "Binarize";
        default: return "Unknown";
    }
}

// Helper to get a pixel value from a surface
static Uint32 get_pixel(SDL_Surface *surface, int x, int y) {
    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(surface->format);
    int bpp = fmt->bytes_per_pixel;
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
    switch (bpp) {
        case 1: return *p;
        case 2: return *(Uint16 *)p;
        case 3:
            if (SDL_BYTEORDER == SDL_BIG_ENDIAN) return p[0] << 16 | p[1] << 8 | p[2];
            else return p[0] | p[1] << 8 | p[2] << 16;
        case 4: return *(Uint32 *)p;
        default: return 0;
    }
}

// Helper to put a pixel value to a surface
static void put_pixel(SDL_Surface *surface, int x, int y, Uint32 pixel) {
    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(surface->format);
    int bpp = fmt->bytes_per_pixel;
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
    switch (bpp) {
        case 1: *p = pixel; break;
        case 2: *(Uint16 *)p = pixel; break;
        case 3:
            if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
                p[0] = (pixel >> 16) & 0xff;
                p[1] = (pixel >> 8) & 0xff;
                p[2] = pixel & 0xff;
            } else {
                p[0] = pixel & 0xff;
                p[1] = (pixel >> 8) & 0xff;
                p[2] = (pixel >> 16) & 0xff;
            }
            break;
        case 4: *(Uint32 *)p = pixel; break;
    }
}


void adjust_gamma_brightness_contrast(SDL_Surface* surface, double gamma, double brightness, double contrast) {
    if (!surface) return;

    SDL_LockSurface(surface);

    int width = surface->w;
    int height = surface->h;
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Palette *palette = SDL_GetSurfacePalette(surface);
    double contrast_factor = (100.0 + contrast) / 100.0;
    contrast_factor *= contrast_factor;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);

            // Apply brightness
            double new_r = r + brightness;
            double new_g = g + brightness;
            double new_b = b + brightness;

            // Apply contrast
            new_r = (new_r - 128) * contrast_factor + 128;
            new_g = (new_g - 128) * contrast_factor + 128;
            new_b = (new_b - 128) * contrast_factor + 128;

            // Apply gamma
            if (gamma != 1.0 && gamma > 0) {
                new_r = pow(new_r / 255.0, 1.0 / gamma) * 255.0;
                new_g = pow(new_g / 255.0, 1.0 / gamma) * 255.0;
                new_b = pow(new_b / 255.0, 1.0 / gamma) * 255.0;
            }

            // Clamp values
            r = (Uint8)fmax(0, fmin(255, new_r));
            g = (Uint8)fmax(0, fmin(255, new_g));
            b = (Uint8)fmax(0, fmin(255, new_b));

            put_pixel(surface, x, y, SDL_MapRGBA(fmt, palette, r, g, b, a));
        }
    }

    SDL_UnlockSurface(surface);
}

void adjust_saturation(SDL_Surface* surface, double saturation) {
    // Stub: Saturation adjustment is complex and requires HSL/HSV conversion.
    (void)saturation; // silence unused parameter warning for now
    if (!surface) return;
    
    
}

void auto_color_balance(SDL_Surface* surface) {
    if (!surface) return;

    SDL_LockSurface(surface);

    int width = surface->w;
    int height = surface->h;
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    unsigned long r_sum = 0, g_sum = 0, b_sum = 0;
    unsigned long pixel_count = width * height;

    // First pass: calculate averages with format-safe pixel access.
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);
            r_sum += r;
            g_sum += g;
            b_sum += b;
        }
    }

    if (pixel_count > 0) {
        double r_avg = (double)r_sum / pixel_count;
        double g_avg = (double)g_sum / pixel_count;
        double b_avg = (double)b_sum / pixel_count;
        double gray_avg = (r_avg + g_avg + b_avg) / 3.0;

        if (r_avg > 0 && g_avg > 0 && b_avg > 0) {
            double r_factor = gray_avg / r_avg;
            double g_factor = gray_avg / g_avg;
            double b_factor = gray_avg / b_avg;

            // Clamp correction strength to avoid extreme shifts on stylized art.
            if (r_factor < 0.80) r_factor = 0.80;
            if (r_factor > 1.20) r_factor = 1.20;
            if (g_factor < 0.80) g_factor = 0.80;
            if (g_factor > 1.20) g_factor = 1.20;
            if (b_factor < 0.80) b_factor = 0.80;
            if (b_factor > 1.20) b_factor = 1.20;

            // Second pass: apply color balance with format-safe writes.
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    Uint32 pixel = get_pixel(surface, x, y);
                    Uint8 r, g, b, a;
                    SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);
                    
                    r = (Uint8)fmin(255, r * r_factor);
                    g = (Uint8)fmin(255, g * g_factor);
                    b = (Uint8)fmin(255, b * b_factor);

                    put_pixel(surface, x, y, SDL_MapRGBA(fmt, palette, r, g, b, a));
                }
            }
        }
    }

    SDL_UnlockSurface(surface);
}

void apply_color_fix(SDL_Surface* surface) {
    if (!surface) return;

    SDL_LockSurface(surface);

    int width = surface->w;
    int height = surface->h;
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    unsigned long r_sum = 0, g_sum = 0, b_sum = 0;
    unsigned long pixel_count = (unsigned long)width * (unsigned long)height;

    // Compute average channel values for a conservative gray-world correction.
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);
            (void)a;
            r_sum += r;
            g_sum += g;
            b_sum += b;
        }
    }

    if (pixel_count == 0 || r_sum == 0 || g_sum == 0 || b_sum == 0) {
        SDL_UnlockSurface(surface);
        return;
    }

    double r_avg = (double)r_sum / (double)pixel_count;
    double g_avg = (double)g_sum / (double)pixel_count;
    double b_avg = (double)b_sum / (double)pixel_count;
    double gray_avg = (r_avg + g_avg + b_avg) / 3.0;

    double r_factor = gray_avg / r_avg;
    double g_factor = gray_avg / g_avg;
    double b_factor = gray_avg / b_avg;

    // Keep color-fix subtle to avoid visible artifacts.
    if (r_factor < 0.90) r_factor = 0.90;
    if (r_factor > 1.10) r_factor = 1.10;
    if (g_factor < 0.90) g_factor = 0.90;
    if (g_factor > 1.10) g_factor = 1.10;
    if (b_factor < 0.90) b_factor = 0.90;
    if (b_factor > 1.10) b_factor = 1.10;

    // Apply white-balance correction.
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);

            double nr = (double)r * r_factor;
            double ng = (double)g * g_factor;
            double nb = (double)b * b_factor;

            r = (Uint8)fmax(0.0, fmin(255.0, nr));
            g = (Uint8)fmax(0.0, fmin(255.0, ng));
            b = (Uint8)fmax(0.0, fmin(255.0, nb));

            put_pixel(surface, x, y, SDL_MapRGBA(fmt, palette, r, g, b, a));
        }
    }

    SDL_UnlockSurface(surface);
}

void apply_gray_world_filter(SDL_Surface* surface) {
    // Same as apply_color_fix - white-balance via gray-world algorithm
    apply_color_fix(surface);
}

void apply_warm_neutralizer_filter(SDL_Surface* surface) {
    if (!surface) return;

    SDL_LockSurface(surface);

    int width = surface->w;
    int height = surface->h;
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    // Reduce red channel dominance (warm cast common in scanned pages)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);

            // Reduce red by 8%, boost blue slightly for balance
            double nr = (double)r * 0.92;
            double nb = (double)b * 1.05;

            r = (Uint8)fmax(0.0, fmin(255.0, nr));
            b = (Uint8)fmax(0.0, fmin(255.0, nb));

            put_pixel(surface, x, y, SDL_MapRGBA(fmt, palette, r, g, b, a));
        }
    }

    SDL_UnlockSurface(surface);
}

void apply_cool_neutralizer_filter(SDL_Surface* surface) {
    if (!surface) return;

    SDL_LockSurface(surface);

    int width = surface->w;
    int height = surface->h;
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    // Reduce blue channel dominance (cool/blue cast)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);

            // Reduce blue by 8%, boost red slightly for balance
            double nr = (double)r * 1.05;
            double nb = (double)b * 0.92;

            r = (Uint8)fmax(0.0, fmin(255.0, nr));
            b = (Uint8)fmax(0.0, fmin(255.0, nb));

            put_pixel(surface, x, y, SDL_MapRGBA(fmt, palette, r, g, b, a));
        }
    }

    SDL_UnlockSurface(surface);
}

void apply_boost_contrast_filter(SDL_Surface* surface) {
    if (!surface) return;

    SDL_LockSurface(surface);

    int width = surface->w;
    int height = surface->h;
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    // Pass 1: find min/max per channel
    Uint8 min_r = 255, min_g = 255, min_b = 255;
    Uint8 max_r = 0, max_g = 0, max_b = 0;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);
            (void)a;

            if (r < min_r) min_r = r;
            if (g < min_g) min_g = g;
            if (b < min_b) min_b = b;
            if (r > max_r) max_r = r;
            if (g > max_g) max_g = g;
            if (b > max_b) max_b = b;
        }
    }

    // Pass 2: normalize/stretch each channel to use full range if range >= 32
    int range_r = (int)max_r - (int)min_r;
    int range_g = (int)max_g - (int)min_g;
    int range_b = (int)max_b - (int)min_b;

    if (range_r >= 32 || range_g >= 32 || range_b >= 32) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                Uint32 pixel = get_pixel(surface, x, y);
                Uint8 r, g, b, a;
                SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);

                double nr = r, ng = g, nb = b;
                
                if (range_r >= 32) {
                    nr = ((double)r - min_r) * 255.0 / (double)range_r;
                }
                if (range_g >= 32) {
                    ng = ((double)g - min_g) * 255.0 / (double)range_g;
                }
                if (range_b >= 32) {
                    nb = ((double)b - min_b) * 255.0 / (double)range_b;
                }

                r = (Uint8)fmax(0.0, fmin(255.0, nr));
                g = (Uint8)fmax(0.0, fmin(255.0, ng));
                b = (Uint8)fmax(0.0, fmin(255.0, nb));

                put_pixel(surface, x, y, SDL_MapRGBA(fmt, palette, r, g, b, a));
            }
        }
    }

    SDL_UnlockSurface(surface);
}

void apply_desaturate_filter(SDL_Surface* surface) {
    if (!surface) return;

    SDL_LockSurface(surface);

    int width = surface->w;
    int height = surface->h;
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    // Desaturate by reducing color saturation to 60% (more vivid B&W)
    double sat_factor = 0.40;  // Reduce saturation to 40% of original

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);

            // Compute luminance (luma)
            double luma = 0.299 * r + 0.587 * g + 0.114 * b;

            // Blend with desaturation
            double nr = luma + (r - luma) * sat_factor;
            double ng = luma + (g - luma) * sat_factor;
            double nb = luma + (b - luma) * sat_factor;

            r = (Uint8)fmax(0.0, fmin(255.0, nr));
            g = (Uint8)fmax(0.0, fmin(255.0, ng));
            b = (Uint8)fmax(0.0, fmin(255.0, nb));

            put_pixel(surface, x, y, SDL_MapRGBA(fmt, palette, r, g, b, a));
        }
    }

    SDL_UnlockSurface(surface);
}

void apply_binarize_filter(SDL_Surface* surface) {
    if (!surface) return;

    SDL_LockSurface(surface);

    int width = surface->w;
    int height = surface->h;
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    // Allocate temporary buffer for smoothed luminance values
    double* luma_buffer = (double*)malloc(width * height * sizeof(double));
    if (!luma_buffer) {
        SDL_UnlockSurface(surface);
        return;
    }

    // Pass 1: Compute luminance for each pixel
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);
            (void)a;

            // Compute relative luminance for brightness
            double luma = 0.299 * r + 0.587 * g + 0.114 * b;
            luma_buffer[y * width + x] = luma;
        }
    }

    // Pass 2: Apply weighted blur for subtle anti-aliasing (center pixel weighted heavily)
    double* smoothed_buffer = (double*)malloc(width * height * sizeof(double));
    if (!smoothed_buffer) {
        free(luma_buffer);
        SDL_UnlockSurface(surface);
        return;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double sum = 0.0;
            double weight_sum = 0.0;

            // Weighted average: center pixel has weight 4, neighbors have less weight
            // Center (x, y): weight 4
            sum += luma_buffer[y * width + x] * 4.0;
            weight_sum += 4.0;

            // Orthogonal neighbors (up/down/left/right): weight 1 each
            int offsets[][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
            for (int i = 0; i < 4; i++) {
                int ny = y + offsets[i][1];
                int nx = x + offsets[i][0];
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    sum += luma_buffer[ny * width + nx] * 1.0;
                    weight_sum += 1.0;
                }
            }

            smoothed_buffer[y * width + x] = (weight_sum > 0) ? sum / weight_sum : luma_buffer[y * width + x];
        }
    }

    // Pass 3: Threshold and write back to surface
    const double threshold = 127.5;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double smoothed_luma = smoothed_buffer[y * width + x];
            Uint8 value = (smoothed_luma >= threshold) ? 255 : 0;

            // Get alpha from original
            Uint32 orig_pixel = get_pixel(surface, x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(orig_pixel, fmt, palette, &r, &g, &b, &a);

            put_pixel(surface, x, y, SDL_MapRGBA(fmt, palette, value, value, value, a));
        }
    }

    free(luma_buffer);
    free(smoothed_buffer);

    SDL_UnlockSurface(surface);
}

void apply_color_filter(SDL_Surface* surface, ColorFilterType filter_type) {
    if (!surface || filter_type == COLOR_FILTER_NONE) return;

    switch (filter_type) {
        case COLOR_FILTER_GRAY_WORLD:
            apply_gray_world_filter(surface);
            break;
        case COLOR_FILTER_WARM_NEUTRALIZER:
            apply_warm_neutralizer_filter(surface);
            break;
        case COLOR_FILTER_COOL_NEUTRALIZER:
            apply_cool_neutralizer_filter(surface);
            break;
        case COLOR_FILTER_BOOST_CONTRAST:
            apply_boost_contrast_filter(surface);
            break;
        case COLOR_FILTER_DESATURATE:
            apply_desaturate_filter(surface);
            break;
        case COLOR_FILTER_BINARIZE:
            apply_binarize_filter(surface);
            break;
        case COLOR_FILTER_NONE:
        case COLOR_FILTER_COUNT:
        default:
            break;
    }
}

void sharpen_image(SDL_Surface* surface, double amount) {
    if (!surface) return;
    // Stub: Sharpening requires convolution which is non-trivial.
    (void)amount; // silence unused parameter warning until implemented
}

void auto_enhance_image(SDL_Surface* surface, const ImageProcessingOptions* options) {
    if (!surface || !options) return;

    // Apply selected color filter (independent of enhancement_enabled)
    if (options->color_filter != COLOR_FILTER_NONE) {
        apply_color_filter(surface, options->color_filter);
    }

    // Keep the legacy enhancement stack behind its own toggle.
    if (options->enhancement_enabled) {
        if (options->color_balance) {
            auto_color_balance(surface);
        }

        if (options->gamma != 1.0 || options->brightness != 0.0 || options->contrast != 0.0) {
            adjust_gamma_brightness_contrast(surface, options->gamma, options->brightness, options->contrast);
        }
        
        if (options->saturation != 1.0) {
            adjust_saturation(surface, options->saturation);
        }

        if (options->sharpen) {
            sharpen_image(surface, 1.0);
        }
    }
}