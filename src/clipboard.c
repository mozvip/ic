#include "clipboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ViewerState viewer;

static const void * get_clipboard_data(void *userdata, const char *mime_type, size_t *size) {
    if (strcmp(mime_type, "image/png") == 0) {
        ImageView *image_view = (ImageView *)userdata;
        if (!image_view) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No image view available for clipboard data\n");
            return NULL;
        }
        // Create PNG data from the view's surface
        SDL_Surface *surface = image_view->surface;
        if (!surface) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No surface available in image view for clipboard data\n");
            return NULL;
        }

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
            // Allocate memory to return
            BYTE *data_copy = (BYTE *)malloc(png_size);
            if (data_copy) {
                memcpy(data_copy, png_data, png_size);
                FreeImage_CloseMemory(mem);
                *size = png_size;
                return data_copy; // memory will be freed by cleanup_clipboard_data
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for clipboard PNG data\n");
            }

        }   
        FreeImage_CloseMemory(mem);
        return NULL;

    }

    return NULL;
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

    SDL_SetClipboardData(get_clipboard_data, cleanup_clipboard_data, view, mime_types, num_mime_types);
}
