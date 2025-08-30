#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <SDL3/SDL.h>
#include <FreeImage.h>
#include <stdbool.h>

// Color correction options
typedef struct {
    double gamma;           // 0.1 - 3.0 (1.0 = no change)
    double brightness;      // -100 to 100 (0 = no change)
    double contrast;        // -100 to 100 (0 = no change)
    double saturation;      // 0.0 - 2.0 (1.0 = no change)
    bool auto_levels;       // Auto contrast/brightness
    bool color_balance;     // Auto color balance
    bool sharpen;          // Apply unsharp mask
    bool enhancement_enabled;
} ImageProcessingOptions;

// Auto-detect and apply optimal corrections
void auto_enhance_image(SDL_Surface* surface, const ImageProcessingOptions* options);

// Specific enhancement functions
void adjust_gamma_brightness_contrast(SDL_Surface* surface, double gamma, double brightness, double contrast);
void adjust_saturation(SDL_Surface* surface, double saturation);
void auto_color_balance(SDL_Surface* surface);
void sharpen_image(SDL_Surface* surface, double amount);

// Get default processing options
ImageProcessingOptions* get_default_processing_options(void);

#endif // IMAGE_PROCESSOR_H
