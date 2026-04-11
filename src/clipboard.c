#include "clipboard.h"
#include "image_loader.h"
#include "temp_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_FREEIMAGE)
#include <FreeImage.h>
#endif

#if defined(HAVE_SDL_IMAGE3)
#include <SDL3_image/SDL_image.h>
#endif

extern struct ViewerState viewer;

// Helper function to load PNG from file into memory
static void *load_file_to_memory(const char *filename, size_t *out_size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open temporary clipboard file: %s\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid temporary clipboard file size\n");
        fclose(fp);
        return NULL;
    }

    void *data = malloc(file_size);
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for clipboard file data\n");
        fclose(fp);
        return NULL;
    }

    if (fread(data, 1, file_size, fp) != (size_t)file_size) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read temporary clipboard file data\n");
        free(data);
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    *out_size = (size_t)file_size;
    return data;
}

// Save PNG using FreeImage backend
static void *save_png_freeimage(SDL_Surface *surface, size_t *out_size) {
#if defined(HAVE_FREEIMAGE)
    SDL_LockSurface(surface);
    FIBITMAP *bitmap = FreeImage_ConvertFromRawBits((BYTE *)surface->pixels,
                                                    surface->w,
                                                    surface->h,
                                                    surface->pitch,
                                                    24,
                                                    0xFF0000,  // Red mask
                                                    0x00FF00,  // Green mask
                                                    0x0000FF,  // Blue mask
                                                    true);     // Top-down
    SDL_UnlockSurface(surface);

    if (!bitmap) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to convert SDL surface to FreeImage bitmap\n");
        return NULL;
    }

    // Use FreeImage to encode surface to PNG in memory
    FIMEMORY *mem = FreeImage_OpenMemory(0, 0);
    if (!mem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open FreeImage memory stream\n");
        FreeImage_Unload(bitmap);
        return NULL;
    }

    if (!FreeImage_SaveToMemory(FIF_PNG, bitmap, mem, 0)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save bitmap to PNG in memory\n");
        FreeImage_Unload(bitmap);
        FreeImage_CloseMemory(mem);
        return NULL;
    }

    FreeImage_Unload(bitmap);
    BYTE *png_data = NULL;
    DWORD png_size = 0;
    FreeImage_AcquireMemory(mem, &png_data, &png_size);

    if (png_data && png_size > 0) {
        // Allocate separate memory to return
        void *data_copy = malloc(png_size);
        if (data_copy) {
            memcpy(data_copy, png_data, png_size);
            FreeImage_CloseMemory(mem);
            *out_size = png_size;
            return data_copy;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for clipboard PNG data\n");
        }
    }

    FreeImage_CloseMemory(mem);
    return NULL;
#else
    (void)surface;
    (void)out_size;
    return NULL;
#endif
}

// Save PNG using SDL_Image backend (via temporary file)
static void *save_png_sdl_image(SDL_Surface *surface, size_t *out_size) {
#if defined(HAVE_SDL_IMAGE3)
    // Create a temporary file for PNG under the app preference path
    char temp_filename[512];
    if (!temp_utils_build_file_path(temp_filename, sizeof(temp_filename),
                                    "ic_clipboard", ".png")) {
        return NULL;
    }

    // Save to temporary PNG file
    if (!IMG_SavePNG(surface, temp_filename)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, 
                     "Failed to save PNG to temporary file via SDL_Image: %s\n", SDL_GetError());
        return NULL;
    }

    // Load the PNG file into memory
    void *data = load_file_to_memory(temp_filename, out_size);

    // Remove temporary file
    if (remove(temp_filename) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to delete temporary clipboard file: %s\n", temp_filename);
    }

    return data;
#else
    (void)surface;
    (void)out_size;
    return NULL;
#endif
}

static const void *get_clipboard_data(void *userdata, const char *mime_type, size_t *size) {
    if (strcmp(mime_type, "image/png") != 0) {
        return NULL;
    }

    ImageView *image_view = (ImageView *)userdata;
    if (!image_view) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No image view available for clipboard data\n");
        return NULL;
    }

    SDL_Surface *surface = image_view->surface;
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No surface available in image view for clipboard data\n");
        return NULL;
    }

    // Get the active image backend and use it dynamically
    ImageBackend backend = image_loader_get_active_backend();

    void *png_data = NULL;

    // Try primary backend first
    if (backend == IMAGE_BACKEND_FREEIMAGE) {
        png_data = save_png_freeimage(surface, size);
        // Fallback to SDL_Image if FreeImage fails and is available
        if (!png_data && image_loader_backend_available(IMAGE_BACKEND_SDL_IMAGE)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "FreeImage PNG save failed, falling back to SDL_Image\n");
            png_data = save_png_sdl_image(surface, size);
        }
    } else if (backend == IMAGE_BACKEND_SDL_IMAGE) {
        png_data = save_png_sdl_image(surface, size);
        // Fallback to FreeImage if SDL_Image fails and is available
        if (!png_data && image_loader_backend_available(IMAGE_BACKEND_FREEIMAGE)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL_Image PNG save failed, falling back to FreeImage\n");
            png_data = save_png_freeimage(surface, size);
        }
    } else {
        // AUTO or unknown backend - try both
        png_data = save_png_freeimage(surface, size);
        if (!png_data) {
            png_data = save_png_sdl_image(surface, size);
        }
    }

    if (!png_data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No image backend available to save PNG for clipboard\n");
    }

    return png_data;
}

static void cleanup_clipboard_data(void *userdata) {
    free(userdata);
}

// Copy the currently displayed view (after cropping, combined pages) to the system clipboard
void copy_current_view_to_clipboard(void) {
    if (!viewer.current_view_node) return;
    ImageView *view = viewer.current_view_node;

    const char **mime_types = malloc(1 * sizeof(char*));
    if (!mime_types) return;
    mime_types[0] = strdup("image/png");
    int num_mime_types = 1;

    if (!SDL_SetClipboardData(get_clipboard_data, cleanup_clipboard_data, view, mime_types, num_mime_types)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to set clipboard image data: %s", SDL_GetError());
        free((void *)mime_types[0]);
        free(mime_types);
    }
}
