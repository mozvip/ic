#include "image_loader.h"
#include "image_processor.h"
#include <FreeImage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

FIBITMAP *load_image_file(const char *filename) {
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

    FreeImage_FlipVertical(bitmap24);
    
    return bitmap24;
}

SDL_Surface* create_surface(FIBITMAP *bitmap, ImageProcessingOptions *options) {
    if (!bitmap || !freeimage_initialized) {
        return NULL;
    }
    
    // Get image properties
    int width = FreeImage_GetWidth(bitmap);
    int height = FreeImage_GetHeight(bitmap);
    int pitch = FreeImage_GetPitch(bitmap);
    
    // Create SDL surface with BGR24 format
    SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_BGR24, FreeImage_GetBits(bitmap), pitch);
    if (!surface) {
        fprintf(stderr, "Failed to create SDL surface: %s\n", SDL_GetError());
        FreeImage_Unload(bitmap);
        return NULL;
    }

    // Apply enhancements on the SDL_Surface
    if (options->enhancement_enabled) {
        auto_enhance_image(surface, options);
    }

    return surface;
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