#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <SDL3/SDL.h>
#include <FreeImage.h>
#include <stdbool.h>

// Color correction options
typedef struct {
    bool enhancement_enabled;
    double gamma;           // 0.1 - 3.0 (1.0 = no change)
    double brightness;      // -100 to 100 (0 = no change)
    double contrast;        // -100 to 100 (0 = no change)
    double saturation;      // 0.0 - 2.0 (1.0 = no change)
    bool auto_levels;       // Auto contrast/brightness
    bool color_balance;     // Auto color balance
    bool sharpen;          // Apply unsharp mask
} ImageProcessingOptions;

// Auto-detect and apply optimal corrections
FIBITMAP* auto_enhance_image(FIBITMAP* bitmap, ImageProcessingOptions* options);

// Specific enhancement functions
FIBITMAP* adjust_gamma(FIBITMAP* bitmap, double gamma);
FIBITMAP* adjust_brightness_contrast(FIBITMAP* bitmap, double brightness, double contrast);
FIBITMAP* adjust_saturation(FIBITMAP* bitmap, double saturation);
FIBITMAP* auto_color_balance(FIBITMAP* bitmap);
FIBITMAP* sharpen_image(FIBITMAP* bitmap, double amount);

// Get default processing options
ImageProcessingOptions* get_default_processing_options(void);

#endif // IMAGE_PROCESSOR_H
