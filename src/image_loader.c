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
    int channels;
    unsigned char *data;
    bool is_valid;
} Image;
#endif

static bool freeimage_initialized = false;

// Supported file extensions
static const char* supported_extensions[] = {
    ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif", 
    ".gif", ".ico", ".webp", ".tga", ".psd", NULL
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

Image* image_load(const char *filename) {
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
    
    // Convert to 32-bit RGBA
    FIBITMAP *bitmap32 = FreeImage_ConvertTo32Bits(bitmap);
    FreeImage_Unload(bitmap);
    
    if (!bitmap32) {
        fprintf(stderr, "Failed to convert image to 32-bit: %s\n", filename);
        return NULL;
    }
    
    // Get image properties
    int width = FreeImage_GetWidth(bitmap32);
    int height = FreeImage_GetHeight(bitmap32);
    int pitch = FreeImage_GetPitch(bitmap32);
    
    // Create our image structure
    Image *image = malloc(sizeof(Image));
    if (!image) {
        FreeImage_Unload(bitmap32);
        return NULL;
    }
    
    image->width = width;
    image->height = height;
    image->channels = 4; // RGBA
    image->data = malloc(width * height * 4);
    image->is_valid = false;
    
    if (!image->data) {
        free(image);
        FreeImage_Unload(bitmap32);
        return NULL;
    }
    
    // Copy pixel data (FreeImage uses BGR, we want RGB)
    BYTE *bits = FreeImage_GetBits(bitmap32);
    for (int y = 0; y < height; y++) {
        BYTE *line = bits + (height - 1 - y) * pitch; // FreeImage is upside down
        for (int x = 0; x < width; x++) {
            int src_idx = x * 4;
            int dst_idx = (y * width + x) * 4;
            
            // Convert BGRA to RGBA
            image->data[dst_idx + 0] = line[src_idx + 2]; // R
            image->data[dst_idx + 1] = line[src_idx + 1]; // G
            image->data[dst_idx + 2] = line[src_idx + 0]; // B
            image->data[dst_idx + 3] = line[src_idx + 3]; // A
        }
    }
    
    image->is_valid = true;
    FreeImage_Unload(bitmap32);
    
    return image;
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
    
    // Convert to 32-bit RGBA
    FIBITMAP *bitmap32 = FreeImage_ConvertTo32Bits(bitmap);
    FreeImage_Unload(bitmap);
    
    if (!bitmap32) {
        fprintf(stderr, "Failed to convert image to 32-bit: %s\n", filename);
        return NULL;
    }
    
    // Apply quality enhancements if enabled
    FIBITMAP *enhanced = bitmap32;
    if (options->enhancement_enabled) {
        FIBITMAP *temp = auto_enhance_image(bitmap32, options);
        if (temp) {
            FreeImage_Unload(bitmap32);
            enhanced = temp;
        }
    }
    
    // Get image properties
    int width = FreeImage_GetWidth(enhanced);
    int height = FreeImage_GetHeight(enhanced);
    int pitch = FreeImage_GetPitch(enhanced);
    
    // Create SDL surface
    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        fprintf(stderr, "Failed to create SDL surface: %s\n", SDL_GetError());
        FreeImage_Unload(enhanced);
        return NULL;
    }
    
    // Copy pixel data (FreeImage uses BGR, SDL expects RGB)
    BYTE *src_bits = FreeImage_GetBits(enhanced);
    uint8_t *dst_pixels = (uint8_t*)surface->pixels;
    
    for (int y = 0; y < height; y++) {
        BYTE *src_line = src_bits + (height - 1 - y) * pitch; // FreeImage is upside down
        uint8_t *dst_line = dst_pixels + y * surface->pitch;
        
        for (int x = 0; x < width; x++) {
            int src_idx = x * 4;
            int dst_idx = x * 4;
            
            // Convert BGRA to RGBA
            dst_line[dst_idx + 0] = src_line[src_idx + 2]; // R
            dst_line[dst_idx + 1] = src_line[src_idx + 1]; // G
            dst_line[dst_idx + 2] = src_line[src_idx + 0]; // B
            dst_line[dst_idx + 3] = src_line[src_idx + 3]; // A
        }
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