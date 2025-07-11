#include "image_loader.h"
#include "image_processor.h"
#include <FreeImage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// Forward declaration of Image struct if not already defined
#ifndef IMAGE_STRUCT_DEFINED
#define IMAGE_STRUCT_DEFINED
typedef struct Image {
    int width;
    int height;
    unsigned char *data;
    bool is_valid;
} Image;
#endif

static bool freeimage_initialized = false;

// Supported file extensions
static const char* supported_extensions[] = {
    ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif", 
    ".gif", ".webp", ".tga", ".psd", NULL
};

bool image_loader_init(void) {
    if (freeimage_initialized) {
        return true;
    }
    
    FreeImage_Initialise(FALSE);
    freeimage_initialized = true;
    
    printf("FreeImage version: %s\n", FreeImage_GetVersion());
    return true;
}

void image_loader_cleanup(void) {
    if (freeimage_initialized) {
        FreeImage_DeInitialise();
        freeimage_initialized = false;
    }
}

SDL_Surface* image_load_surface(const char *filename, ImageProcessingOptions *options) {
    if (!filename || !freeimage_initialized) {
        return NULL;
    }
    
    // Determine file format
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(filename, 0);
    if (fif == FIF_UNKNOWN) {
        fif = FreeImage_GetFIFFromFilename(filename);
    }
    
    if (fif == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fif)) {
        fprintf(stderr, "Unsupported image format: %s\n", filename);
        return NULL;
    }
    
    // Load the image
    FIBITMAP *bitmap = FreeImage_Load(fif, filename, 0);
    if (!bitmap) {
        fprintf(stderr, "Failed to load image: %s\n", filename);
        return NULL;
    }
    
    // Convert to 24-bit BGR (no alpha)
    FIBITMAP *bitmap24 = FreeImage_ConvertTo24Bits(bitmap);
    FreeImage_Unload(bitmap);
    
    if (!bitmap24) {
        fprintf(stderr, "Failed to convert image to 24-bit: %s\n", filename);
        return NULL;
    }
    
    // Apply quality enhancements if enabled
    FIBITMAP *enhanced = bitmap24;
    if (options->enhancement_enabled) {
        FIBITMAP *temp = auto_enhance_image(bitmap24, options);
        if (temp) {
            FreeImage_Unload(bitmap24);
            enhanced = temp;
        }
    }
    
    // Get image properties
    int width = FreeImage_GetWidth(enhanced);
    int height = FreeImage_GetHeight(enhanced);
    int pitch = FreeImage_GetPitch(enhanced);
    
    // Create SDL surface with BGR24 format
    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_BGR24);
    if (!surface) {
        fprintf(stderr, "Failed to create SDL surface: %s\n", SDL_GetError());
        FreeImage_Unload(enhanced);
        return NULL;
    }
    
    // Copy pixel data (FreeImage uses BGR, SDL expects BGR)
    BYTE *src_bits = FreeImage_GetBits(enhanced);
    uint8_t *dst_pixels = (uint8_t*)surface->pixels;
    
    for (int y = 0; y < height; y++) {
        BYTE *src_line = src_bits + (height - 1 - y) * pitch; // FreeImage is upside down
        uint8_t *dst_line = dst_pixels + y * surface->pitch;
        memcpy(dst_line, src_line, width * 3); // 3 bytes per pixel (BGR)
    }
    
    FreeImage_Unload(enhanced);
    return surface;
}

void image_free(Image *image) {
    if (image) {
        if (image->data) {
            free(image->data);
        }
        free(image);
    }
}

const char** image_get_supported_extensions(void) {
    return supported_extensions;
}

bool image_is_supported(const char *filename) {
    if (!filename) return false;
    
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    
    for (int i = 0; supported_extensions[i]; i++) {
        if (strcasecmp(ext, supported_extensions[i]) == 0) {
            return true;
        }
    }
    
    return false;
}