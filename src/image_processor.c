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
    options->auto_levels = true;
    options->color_balance = false;
    options->sharpen = false;

    return options;
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
    if (!surface) return;
    
    
}

void auto_color_balance(SDL_Surface* surface) {
    SDL_LockSurface(surface);

    int width = surface->w;
    int height = surface->h;
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    unsigned long r_sum = 0, g_sum = 0, b_sum = 0;
    unsigned long pixel_count = width * height;

    // First pass: calculate averages using direct memory access
    Uint32 *pixels = (Uint32 *)surface->pixels;
    int pitch_in_pixels = surface->pitch / sizeof(Uint32);
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel = pixels[y * pitch_in_pixels + x];
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

            // Second pass: apply color balance using direct memory access
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    Uint32 pixel = pixels[y * pitch_in_pixels + x];
                    Uint8 r, g, b, a;
                    SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);
                    
                    r = (Uint8)fmin(255, r * r_factor);
                    g = (Uint8)fmin(255, g * g_factor);
                    b = (Uint8)fmin(255, b * b_factor);

                    pixels[y * pitch_in_pixels + x] = SDL_MapRGBA(fmt, palette, r, g, b, a);
                }
            }
        }
    }

    SDL_UnlockSurface(surface);
}

void sharpen_image(SDL_Surface* surface, double amount) {
    if (!surface) return;
    // Stub: Sharpening requires convolution which is non-trivial.
    
}

void auto_enhance_image(SDL_Surface* surface, const ImageProcessingOptions* options) {

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