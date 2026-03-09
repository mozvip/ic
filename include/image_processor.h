#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Color filter types for specialized color corrections
typedef enum {
    COLOR_FILTER_NONE = 0,           // No filter
    COLOR_FILTER_GRAY_WORLD,         // White-balance correction
    COLOR_FILTER_WARM_NEUTRALIZER,   // Reduce warm/yellow cast
    COLOR_FILTER_COOL_NEUTRALIZER,   // Reduce cool/blue cast
    COLOR_FILTER_BOOST_CONTRAST,     // Enhance local contrast
    COLOR_FILTER_DESATURATE,         // Reduce color saturation for faded pages
    COLOR_FILTER_BINARIZE,           // Convert to 1-bit black and white
    COLOR_FILTER_COUNT
} ColorFilterType;

// Color correction options
typedef struct {
    double gamma;           // 0.1 - 3.0 (1.0 = no change)
    double brightness;      // -100 to 100 (0 = no change)
    double contrast;        // -100 to 100 (0 = no change)
    double saturation;      // 0.0 - 2.0 (1.0 = no change)
    bool color_balance;     // Auto color balance
    ColorFilterType color_filter;    // Selected color filter
    bool sharpen;          // Apply unsharp mask
    bool enhancement_enabled;
} ImageProcessingOptions;

// Auto-detect and apply optimal corrections
void auto_enhance_image(SDL_Surface* surface, const ImageProcessingOptions* options);

// Specific enhancement functions
void adjust_gamma_brightness_contrast(SDL_Surface* surface, double gamma, double brightness, double contrast);
void adjust_saturation(SDL_Surface* surface, double saturation);
void auto_color_balance(SDL_Surface* surface);

// Individual color filter implementations
void apply_gray_world_filter(SDL_Surface* surface);
void apply_warm_neutralizer_filter(SDL_Surface* surface);
void apply_cool_neutralizer_filter(SDL_Surface* surface);
void apply_boost_contrast_filter(SDL_Surface* surface);
void apply_desaturate_filter(SDL_Surface* surface);
void apply_binarize_filter(SDL_Surface* surface);

// Main filter dispatcher
void apply_color_filter(SDL_Surface* surface, ColorFilterType filter_type);

void sharpen_image(SDL_Surface* surface, double amount);

// Filter name and helper functions
const char* color_filter_get_name(ColorFilterType filter_type);

// Get default processing options
ImageProcessingOptions* get_default_processing_options(void);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_PROCESSOR_H
