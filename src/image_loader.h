#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include "image_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	IMAGE_BACKEND_AUTO = 0,
	IMAGE_BACKEND_FREEIMAGE,
	IMAGE_BACKEND_SDL_IMAGE,
	IMAGE_BACKEND_COUNT
} ImageBackend;

// Initialize image loading subsystem and choose an active backend.
bool image_loader_init(void);

// Cleanup image loading subsystem.
void image_loader_cleanup(void);

// Set preferred backend. When set to AUTO, FreeImage is preferred then SDL_image.
// Returns false if the requested backend is unavailable.
bool image_loader_set_preferred_backend(ImageBackend backend);

// Returns the currently active backend after init/selection.
ImageBackend image_loader_get_active_backend(void);

// Returns true when a backend is available in this build/runtime.
bool image_loader_backend_available(ImageBackend backend);

// Human-readable backend names.
const char *image_loader_backend_name(ImageBackend backend);

// Parse backend name strings: auto, freeimage, sdl_image.
ImageBackend image_loader_parse_backend(const char *name);

// Decode an image to a surface and optionally apply enhancement options.
SDL_Surface *image_load_surface(const char *filename, ImageProcessingOptions *options);

// Check if file extension is supported
bool image_is_supported(const char *filename);

// EXIF / image metadata extraction.
#define IMAGE_EXIF_MAX_ENTRIES 96
#define IMAGE_EXIF_KEY_LEN 64
#define IMAGE_EXIF_VALUE_LEN 256

typedef struct {
	char key[IMAGE_EXIF_KEY_LEN];
	char value[IMAGE_EXIF_VALUE_LEN];
} ImageExifEntry;

// Returns true when metadata extraction is supported in this build.
bool image_loader_exif_available(void);

// Extract EXIF/metadata tags from an image file into caller-provided entries.
// Returns the number of entries filled (0 when none or unsupported).
int image_load_exif(const char *filename, ImageExifEntry *entries, int max_entries);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_LOADER_H
