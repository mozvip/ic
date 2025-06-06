#include "image_processor.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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

FIBITMAP* adjust_gamma_brightness_contrast(FIBITMAP* bitmap, double gamma, double brightness, double contrast) {
    if (!bitmap || gamma <= 0) return NULL;
    
    FIBITMAP* result = FreeImage_Clone(bitmap);
    if (!result) return NULL;

    if (FreeImage_AdjustGamma(result, gamma) &&
        FreeImage_AdjustBrightness(result, brightness) &&
        FreeImage_AdjustContrast(result, contrast)) {
        return result;
    }
    
    FreeImage_Unload(result);
    return NULL;
}

FIBITMAP* adjust_saturation(FIBITMAP* bitmap, double saturation) {
    if (!bitmap) return NULL;
    
    FIBITMAP* result = FreeImage_Clone(bitmap);
    if (!result) return NULL;
    
    // Convert saturation from 0.0-2.0 to -100 to 100 range
    double sat_percent = (saturation - 1.0) * 100.0;
    
    if (FreeImage_AdjustColors(result, 0, 0, sat_percent, FALSE)) {
        return result;
    }
    
    FreeImage_Unload(result);
    return NULL;
}

FIBITMAP* auto_color_balance(FIBITMAP* bitmap) {
    if (!bitmap) return NULL;
    
    FIBITMAP* result = FreeImage_Clone(bitmap);
    if (!result) return NULL;
    
    // Get image statistics for auto white balance
    int width = FreeImage_GetWidth(result);
    int height = FreeImage_GetHeight(result);
    
    if (width == 0 || height == 0) {
        FreeImage_Unload(result);
        return NULL;
    }
    
    // Simple auto white balance using gray world assumption
    unsigned long r_sum = 0, g_sum = 0, b_sum = 0;
    unsigned long pixel_count = 0;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            RGBQUAD color;
            if (FreeImage_GetPixelColor(result, x, y, &color)) {
                r_sum += color.rgbRed;
                g_sum += color.rgbGreen;
                b_sum += color.rgbBlue;
                pixel_count++;
            }
        }
    }
    
    if (pixel_count > 0) {
        double r_avg = (double)r_sum / pixel_count;
        double g_avg = (double)g_sum / pixel_count;
        double b_avg = (double)b_sum / pixel_count;
        double gray_avg = (r_avg + g_avg + b_avg) / 3.0;
        
        // Calculate correction factors
        double r_factor = gray_avg / r_avg;
        double g_factor = gray_avg / g_avg;
        double b_factor = gray_avg / b_avg;
        
        // Limit correction to reasonable bounds
        r_factor = fmax(0.5, fmin(2.0, r_factor));
        g_factor = fmax(0.5, fmin(2.0, g_factor));
        b_factor = fmax(0.5, fmin(2.0, b_factor));
        
        // Apply color correction
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                RGBQUAD color;
                if (FreeImage_GetPixelColor(result, x, y, &color)) {
                    color.rgbRed = (BYTE)fmin(255, color.rgbRed * r_factor);
                    color.rgbGreen = (BYTE)fmin(255, color.rgbGreen * g_factor);
                    color.rgbBlue = (BYTE)fmin(255, color.rgbBlue * b_factor);
                    FreeImage_SetPixelColor(result, x, y, &color);
                }
            }
        }
    }
    
    return result;
}

FIBITMAP* sharpen_image(FIBITMAP* bitmap, double amount) {
    if (!bitmap) return NULL;
    
    // Use a simple convolution kernel for sharpening since UnsharpMask may not be available
    FIBITMAP* result = FreeImage_Clone(bitmap);
    if (!result) return NULL;

    
    // For now, return the clone - implement custom sharpening later if needed
    return result;
}

FIBITMAP* auto_enhance_image(FIBITMAP* bitmap, ImageProcessingOptions* options) {
    if (!options || !options->enhancement_enabled) {
        return NULL;
    }
    
    FIBITMAP* current = FreeImage_Clone(bitmap);
    if (!current) return NULL;

    if (options->auto_levels) {
        // Step 1: Auto normalize - apply histogram stretching
        // For now, skip auto normalize and use other enhancements
    }
    
    // Step 2: Auto color balance
    FIBITMAP* balanced = auto_color_balance(current);
    if (balanced) {
        FreeImage_Unload(current);
        current = balanced;
    }
    
    if (options->sharpen) {
        // Step 2: Apply sharpening if enabled
        FIBITMAP* sharpened = sharpen_image(current, 1.0); // Default amount of 1.0
        if (sharpened) {
            FreeImage_Unload(current);
            current = sharpened;
        }
    }

    if (options->gamma != 1.0 || options->brightness != 0.0 || options->contrast != 0.0) {
        // Step 3: Adjust gamma, brightness, and contrast
        FIBITMAP* adjusted = adjust_gamma_brightness_contrast(current, options->gamma, options->brightness, options->contrast);
        if (adjusted) {
            FreeImage_Unload(current);
            current = adjusted;
        }
    }
    
    if (options->saturation != 1.0) {
        // Step 4: Adjust saturation
        FIBITMAP* saturated = adjust_saturation(current, options->saturation);
        if (saturated) {
            FreeImage_Unload(current);
            current = saturated;
        }
    }
    
    return current;
}