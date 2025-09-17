#include "edges.h"
#include <stdlib.h>
#include <math.h>

// Helper function to convert RGB to HSL
// r, g, b, s, l are in [0, 1], h is in [0, 360)
static void rgb_to_hsl(float r, float g, float b, float *h, float *s, float *l) {
    float max_val = fmaxf(fmaxf(r, g), b);
    float min_val = fminf(fminf(r, g), b);
    *l = (max_val + min_val) / 2.0f;

    if (max_val == min_val) {
        *h = 0; // achromatic
        *s = 0;
    } else {
        float d = max_val - min_val;
        *s = (*l > 0.5f) ? d / (2.0f - max_val - min_val) : d / (max_val + min_val);
        if (max_val == r) {
            *h = (g - b) / d + (g < b ? 6.0f : 0);
        } else if (max_val == g) {
            *h = (b - r) / d + 2.0f;
        } else { // max_val == b
            *h = (r - g) / d + 4.0f;
        }
        *h /= 6.0f;
        *h *= 360.0f;
    }
}

// Helper for hsl_to_rgb
static float hue_to_rgb_component(float p, float q, float t) {
    if (t < 0) t += 1.0f;
    if (t > 1) t -= 1.0f;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2.0f) return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

// Helper function to convert HSL to RGB
// r, g, b, s, l are in [0, 1], h is in [0, 360)
static void hsl_to_rgb(float h, float s, float l, float *r, float *g, float *b) {
    if (s == 0) {
        *r = *g = *b = l; // achromatic
    } else {
        float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        float h_norm = h / 360.0f;
        *r = hue_to_rgb_component(p, q, h_norm + 1.0f/3.0f);
        *g = hue_to_rgb_component(p, q, h_norm);
        *b = hue_to_rgb_component(p, q, h_norm - 1.0f/3.0f);
    }
}

// Function to render a horizontal gradient using HSL interpolation
void render_horizontal_gradient_hsl(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color edge_color_rgb, bool edge_color_is_on_left_of_fill) {
    if (rect.w <= 0) return; // Do not render if width is zero or negative

    float r_edge = edge_color_rgb.r / 255.0f;
    float g_edge = edge_color_rgb.g / 255.0f;
    float b_edge = edge_color_rgb.b / 255.0f;

    float h_edge, s_edge, l_edge;
    rgb_to_hsl(r_edge, g_edge, b_edge, &h_edge, &s_edge, &l_edge);

    for (int col = 0; col < (int)rect.w; ++col) {
        float t; // Interpolation factor: 0 for edge_color, 1 for black
        if (edge_color_is_on_left_of_fill) { // Gradient from left (edge_color) to right (black)
            t = (float)col / (float)(rect.w > 1 ? rect.w -1 : 1); // Avoid division by zero if rect.w is 1
        } else { // Gradient from right (edge_color) to left (black)
            t = 1.0f - ((float)col / (float)(rect.w > 1 ? rect.w -1 : 1));
        }
        // Clamp t to [0, 1] just in case
        t = fmaxf(0.0f, fminf(1.0f, t));


        // Interpolate S and L towards 0 (black), keep H constant
        float s_interp = s_edge * (1.0f - t);
        float l_interp = l_edge * (1.0f - t);

        float r_interp, g_interp, b_interp;
        hsl_to_rgb(h_edge, s_interp, l_interp, &r_interp, &g_interp, &b_interp);

        SDL_SetRenderDrawColor(renderer,
                               (Uint8)(r_interp * 255.0f),
                               (Uint8)(g_interp * 255.0f),
                               (Uint8)(b_interp * 255.0f),
                               255);
        SDL_RenderLine(renderer, rect.x + col, rect.y, rect.x + col, rect.y + rect.h -1.0f);
    }
}

// Function to get the most prominent color from a surface region
static SDL_Color get_dominant_color(SDL_Surface *surface, int x, int y, int width, int height) {
    // Default color (black)
    SDL_Color dominant = {0, 0, 0, 255};
    
    // Use a different approach to avoid the large array allocation
    // We'll use color buckets with fewer bits per channel
    #define COLOR_BITS 5
    #define COLOR_BUCKETS (1 << COLOR_BITS)
    #define COLOR_MASK ((1 << COLOR_BITS) - 1)
    
    // Allocate the frequency counter on the heap instead of stack
    unsigned int *color_freq = calloc(COLOR_BUCKETS * COLOR_BUCKETS * COLOR_BUCKETS, sizeof(unsigned int));
    if (!color_freq) return dominant;
    
    unsigned int max_freq = 0;
    int dominant_index = 0;
    
    // Get pixel format
    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(surface->format);
    int bpp = fmt->bytes_per_pixel;
    
    // Scan the specified region with bounds checking
    int sample_step = 2; // Sample every 2nd pixel to speed up analysis
    
    uint8_t *pixels = (uint8_t *)surface->pixels;
    int pitch = surface->pitch;
    
    for (int j = y; j < y + height; j += sample_step) {
        for (int i = x; i < x + width; i += sample_step) {
            // Skip if outside bounds
            if (i < 0 || i >= surface->w || j < 0 || j >= surface->h) continue;
            
            // Extract the pixel
            uint8_t *p = pixels + j * pitch + i * bpp;
            uint32_t pixel = 0;
            
            // Be very careful with pixel access
            switch (bpp) {
                case 1: pixel = *p; break;
                case 2: pixel = *(uint16_t *)p; break;
                case 3: 
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                    #else
                        pixel = p[0] | p[1] << 8 | p[2] << 16; 
                    #endif
                    break;
                case 4: pixel = *(uint32_t *)p; break;
                default: continue; // Skip unknown formats
            }
            
            // Convert to RGB
            uint8_t r, g, b, a;
            SDL_GetRGBA(pixel, fmt, SDL_GetSurfacePalette(surface), &r, &g, &b, &a);
            
            // Skip almost black or almost white pixels
            if ((r < 15 && g < 15 && b < 15) || (r > 240 && g > 240 && b > 240)) {
                continue;
            }
            
            // Reduce color depth to fit in our buckets
            r >>= (8 - COLOR_BITS);
            g >>= (8 - COLOR_BITS);
            b >>= (8 - COLOR_BITS);
            
            // Calculate bucket index
            int bucket_index = (r * COLOR_BUCKETS * COLOR_BUCKETS) + (g * COLOR_BUCKETS) + b;
            
            // Increment frequency
            color_freq[bucket_index]++;
            if (color_freq[bucket_index] > max_freq) {
                max_freq = color_freq[bucket_index];
                dominant_index = bucket_index;
            }
        }
    }
    
    // Convert bucket index back to RGB
    if (max_freq > 0) {
        int r = (dominant_index / (COLOR_BUCKETS * COLOR_BUCKETS)) & COLOR_MASK;
        int g = (dominant_index / COLOR_BUCKETS) & COLOR_MASK;
        int b = dominant_index & COLOR_MASK;
        
        // Convert back to 8-bit channels
        dominant.r = (r << (8 - COLOR_BITS)) | (r >> (2 * COLOR_BITS - 8));
        dominant.g = (g << (8 - COLOR_BITS)) | (g >> (2 * COLOR_BITS - 8));
        dominant.b = (b << (8 - COLOR_BITS)) | (b >> (2 * COLOR_BITS - 8));
    }
    
    free(color_freq);
    return dominant;
}

// This function analyzes the image and extracts the dominant color from the left edge
void analyze_image_left_edge(SDL_Surface *surface, SDL_FRect *crop_rect, SDL_Color *left_color) {
    // Default to black if something goes wrong
    *left_color = (SDL_Color){0, 0, 0, 255};
    
    // Get image dimensions
    int width = crop_rect->w;
    int height = crop_rect->h;

    // Sample pixels from the left edge (8% of width)
    int edge_width = width * 0.08;
    if (edge_width < 1) edge_width = 1;
    if (edge_width > width) edge_width = width; // Cap at image width

    // Get dominant color from left edge
    *left_color = get_dominant_color(surface, crop_rect->x, crop_rect->y, edge_width, height);
}

// This function analyzes the image and extracts the dominant color from the right edge
void analyze_image_right_edge(SDL_Surface *surface, SDL_FRect *crop_rect, SDL_Color *right_color) {
    // Default to black if something goes wrong
    *right_color = (SDL_Color){0, 0, 0, 255};

    // Get image dimensions
    int width = crop_rect->w;
    int height = crop_rect->h;

    // Sample pixels from the right edge (8% of width)
    int edge_width = width * 0.08;
    if (edge_width < 1) edge_width = 1;
    if (edge_width > width) edge_width = width; // Cap at image width
    
    // Get dominant color from right edge
    *right_color = get_dominant_color(surface, crop_rect->x + crop_rect->w - edge_width, crop_rect->y, edge_width, height);
}