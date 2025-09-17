#include "edges.h"
#include <stdlib.h>

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