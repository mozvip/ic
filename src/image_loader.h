#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include "image_processor.h"

// Initialize FreeImage library
bool image_loader_init(void);

// Cleanup FreeImage library
void image_loader_cleanup(void);

// Load an image from file
FIBITMAP *load_image_file(const char *filename);
SDL_Surface* create_surface(FIBITMAP *bitmap, ImageProcessingOptions *options);

// Check if file extension is supported
bool image_is_supported(const char *filename);

#endif // IMAGE_LOADER_H
