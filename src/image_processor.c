#include "image_processor.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

ImageProcessingOptions get_default_processing_options(void) {
    ImageProcessingOptions options = {
        .gamma = 1.0,
        .brightness = 0.0,
        .contrast = 0.0,
        .saturation = 1.0,
        .auto_levels = true,
        .color_balance = true,
        .sharpen = false
    };
    return options;
}

FIBITMAP* adjust_gamma(FIBITMAP* bitmap, double gamma) {
    if (!bitmap || gamma <= 0) return NULL;
    
    FIBITMAP* result = FreeImage_Clone(bitmap);
    if (!result) return NULL;
    
    if (FreeImage_AdjustGamma(result, gamma)) {
        return result;
    }
    
    FreeImage_Unload(result);
    return NULL;
}

FIBITMAP* adjust_brightness_contrast(FIBITMAP* bitmap, double brightness, double contrast) {
    if (!bitmap) return NULL;
    
    FIBITMAP* result = FreeImage_Clone(bitmap);
    if (!result) return NULL;
    
    if (FreeImage_AdjustBrightness(result, brightness) && 
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

FIBITMAP* auto_enhance_image(FIBITMAP* bitmap) {
    if (!bitmap) return NULL;
    
    FIBITMAP* current = FreeImage_Clone(bitmap);
    if (!current) return NULL;
    
    // Step 1: Auto normalize - implement histogram stretching manually
    // For now, skip auto normalize and use other enhancements
    
    // Step 2: Auto color balance
    FIBITMAP* balanced = auto_color_balance(current);
    if (balanced) {
        FreeImage_Unload(current);
        current = balanced;
    }
    
    // Step 3: Slight gamma correction for better midtones
    FIBITMAP* gamma_corrected = adjust_gamma(current, 1.1);
    if (gamma_corrected) {
        FreeImage_Unload(current);
        current = gamma_corrected;
    }
    
    // Step 4: Enhance saturation slightly
    FIBITMAP* saturated = adjust_saturation(current, 1.1);
    if (saturated) {
        FreeImage_Unload(current);
        current = saturated;
    }
    
    return current;
}

FIBITMAP* process_image_quality(FIBITMAP* bitmap, const ImageProcessingOptions* options) {
    if (!bitmap || !options) return NULL;
    
    FIBITMAP* current = FreeImage_Clone(bitmap);
    if (!current) return NULL;
    
    // Auto levels (histogram normalization)
    if (options->auto_levels) {
        // Manual histogram stretching - find min/max values and stretch
        int width = FreeImage_GetWidth(current);
        int height = FreeImage_GetHeight(current);
        
        BYTE min_r = 255, min_g = 255, min_b = 255;
        BYTE max_r = 0, max_g = 0, max_b = 0;
        
        // Find min/max values
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                RGBQUAD color;
                if (FreeImage_GetPixelColor(current, x, y, &color)) {
                    if (color.rgbRed < min_r) min_r = color.rgbRed;
                    if (color.rgbRed > max_r) max_r = color.rgbRed;
                    if (color.rgbGreen < min_g) min_g = color.rgbGreen;
                    if (color.rgbGreen > max_g) max_g = color.rgbGreen;
                    if (color.rgbBlue < min_b) min_b = color.rgbBlue;
                    if (color.rgbBlue > max_b) max_b = color.rgbBlue;
                }
            }
        }
        
        // Apply stretching if there's room for improvement
        if (max_r > min_r && max_g > min_g && max_b > min_b) {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    RGBQUAD color;
                    if (FreeImage_GetPixelColor(current, x, y, &color)) {
                        color.rgbRed = (BYTE)(255.0 * (color.rgbRed - min_r) / (max_r - min_r));
                        color.rgbGreen = (BYTE)(255.0 * (color.rgbGreen - min_g) / (max_g - min_g));
                        color.rgbBlue = (BYTE)(255.0 * (color.rgbBlue - min_b) / (max_b - min_b));
                        FreeImage_SetPixelColor(current, x, y, &color);
                    }
                }
            }
        }
    }
    
    // Auto color balance
    if (options->color_balance) {
        FIBITMAP* balanced = auto_color_balance(current);
        if (balanced) {
            FreeImage_Unload(current);
            current = balanced;
        }
    }
    
    // Gamma correction
    if (options->gamma != 1.0) {
        FIBITMAP* gamma_corrected = adjust_gamma(current, options->gamma);
        if (gamma_corrected) {
            FreeImage_Unload(current);
            current = gamma_corrected;
        }
    }
    
    // Brightness and contrast
    if (options->brightness != 0.0 || options->contrast != 0.0) {
        FIBITMAP* bc_adjusted = adjust_brightness_contrast(current, options->brightness, options->contrast);
        if (bc_adjusted) {
            FreeImage_Unload(current);
            current = bc_adjusted;
        }
    }
    
    // Saturation
    if (options->saturation != 1.0) {
        FIBITMAP* saturated = adjust_saturation(current, options->saturation);
        if (saturated) {
            FreeImage_Unload(current);
            current = saturated;
        }
    }
    
    // Sharpening (apply last)
    if (options->sharpen) {
        FIBITMAP* sharpened = sharpen_image(current, 0.5);
        if (sharpened) {
            FreeImage_Unload(current);
            current = sharpened;
        }
    }
    
    return current;
}
