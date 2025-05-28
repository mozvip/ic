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

// Auto enhancement option
static bool auto_enhance_enabled = true;

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

void image_loader_set_auto_enhance(bool enabled) {
    auto_enhance_enabled = enabled;
}

bool image_loader_get_auto_enhance(void) {
    return auto_enhance_enabled;
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

SDL_Surface* image_load_surface(const char *filename) {
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
    if (auto_enhance_enabled) {
        FIBITMAP *temp = auto_enhance_image(bitmap32);
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

void image_loader_test_enhancement(const char *input_file, const char *output_file) {
    if (!input_file || !output_file || !freeimage_initialized) {
        printf("Test enhancement: Invalid parameters or FreeImage not initialized\n");
        return;
    }
    
    // Load original image
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(input_file, 0);
    if (fif == FIF_UNKNOWN) {
        fif = FreeImage_GetFIFFromFilename(input_file);
    }
    
    if (fif == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fif)) {
        printf("Test enhancement: Unsupported image format: %s\n", input_file);
        return;
    }
    
    FIBITMAP *original = FreeImage_Load(fif, input_file, 0);
    if (!original) {
        printf("Test enhancement: Failed to load %s\n", input_file);
        return;
    }
    
    FIBITMAP *bitmap32 = FreeImage_ConvertTo32Bits(original);
    FreeImage_Unload(original);
    
    if (!bitmap32) {
        printf("Test enhancement: Failed to convert to 32-bit\n");
        return;
    }
    
    // Apply enhancements
    FIBITMAP *enhanced = auto_enhance_image(bitmap32);
    if (!enhanced) {
        printf("Test enhancement: Enhancement failed\n");
        FreeImage_Unload(bitmap32);
        return;
    }
    
    // Save enhanced image
    FREE_IMAGE_FORMAT output_fif = FreeImage_GetFIFFromFilename(output_file);
    if (output_fif != FIF_UNKNOWN && FreeImage_FIFSupportsWriting(output_fif)) {
        if (FreeImage_Save(output_fif, enhanced, output_file, 0)) {
            printf("Enhanced image saved to: %s\n", output_file);
        } else {
            printf("Failed to save enhanced image to: %s\n", output_file);
        }
    } else {
        printf("Unsupported output format for: %s\n", output_file);
    }
    
    FreeImage_Unload(bitmap32);
    FreeImage_Unload(enhanced);
}
