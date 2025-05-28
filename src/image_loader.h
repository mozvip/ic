#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <SDL3/SDL.h>
#include <stdbool.h>

// Initialize FreeImage library
bool image_loader_init(void);

// Cleanup FreeImage library
void image_loader_cleanup(void);

// Load image from file and return SDL_Surface (replacement for IMG_Load)
SDL_Surface* image_load_surface(const char *filename);

// Check if file extension is supported
bool image_is_supported(const char *filename);

// Enable/disable automatic image enhancement
void image_loader_set_auto_enhance(bool enabled);

#endif // IMAGE_LOADER_H
