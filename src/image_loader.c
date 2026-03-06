#include "image_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#if defined(HAVE_FREEIMAGE)
#include <FreeImage.h>
#endif

#if defined(HAVE_SDL_IMAGE3)
#include <SDL3_image/SDL_image.h>
#endif

static bool loader_initialized = false;
#if defined(HAVE_FREEIMAGE)
static bool freeimage_initialized = false;
#endif
static ImageBackend preferred_backend = IMAGE_BACKEND_AUTO;
static ImageBackend active_backend = IMAGE_BACKEND_AUTO;

// Supported file extensions
static const char *supported_extensions[] = {
    ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif",
    ".gif", ".webp", ".tga", ".psd", NULL
};

static ImageBackend choose_backend(ImageBackend preferred) {
    if (preferred == IMAGE_BACKEND_FREEIMAGE) {
        if (image_loader_backend_available(IMAGE_BACKEND_FREEIMAGE)) {
            return IMAGE_BACKEND_FREEIMAGE;
        }
        return IMAGE_BACKEND_AUTO;
    }

    if (preferred == IMAGE_BACKEND_SDL_IMAGE) {
        if (image_loader_backend_available(IMAGE_BACKEND_SDL_IMAGE)) {
            return IMAGE_BACKEND_SDL_IMAGE;
        }
        return IMAGE_BACKEND_AUTO;
    }

    // AUTO: prefer FreeImage first, then SDL_image.
    if (image_loader_backend_available(IMAGE_BACKEND_FREEIMAGE)) {
        return IMAGE_BACKEND_FREEIMAGE;
    }
    if (image_loader_backend_available(IMAGE_BACKEND_SDL_IMAGE)) {
        return IMAGE_BACKEND_SDL_IMAGE;
    }
    return IMAGE_BACKEND_AUTO;
}

static SDL_Surface *load_surface_freeimage(const char *filename, ImageProcessingOptions *options) {
#if defined(HAVE_FREEIMAGE)
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(filename, 0);
    if (fif == FIF_UNKNOWN) {
        fif = FreeImage_GetFIFFromFilename(filename);
    }

    if (fif == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fif)) {
        fprintf(stderr, "Unsupported image format (FreeImage): %s\n", filename);
        return NULL;
    }

    FIBITMAP *bitmap = FreeImage_Load(fif, filename, JPEG_ACCURATE | JPEG_EXIFROTATE);
    if (!bitmap) {
        fprintf(stderr, "Failed to load image with FreeImage: %s\n", filename);
        return NULL;
    }

    FIBITMAP *bitmap24 = FreeImage_ConvertTo24Bits(bitmap);
    FreeImage_Unload(bitmap);
    if (!bitmap24) {
        fprintf(stderr, "Failed to convert image to 24-bit: %s\n", filename);
        return NULL;
    }

    FreeImage_FlipVertical(bitmap24);

    int width = (int)FreeImage_GetWidth(bitmap24);
    int height = (int)FreeImage_GetHeight(bitmap24);
    int pitch = (int)FreeImage_GetPitch(bitmap24);

    SDL_Surface *borrowed = SDL_CreateSurfaceFrom(
        width,
        height,
        SDL_PIXELFORMAT_BGR24,
        FreeImage_GetBits(bitmap24),
        pitch
    );
    if (!borrowed) {
        fprintf(stderr, "Failed to create SDL surface from FreeImage buffer: %s\n", SDL_GetError());
        FreeImage_Unload(bitmap24);
        return NULL;
    }

    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_BGR24);
    if (!surface) {
        fprintf(stderr, "Failed to create SDL surface copy: %s\n", SDL_GetError());
        SDL_DestroySurface(borrowed);
        FreeImage_Unload(bitmap24);
        return NULL;
    }

    if (!SDL_BlitSurface(borrowed, NULL, surface, NULL)) {
        fprintf(stderr, "Failed to blit FreeImage surface copy: %s\n", SDL_GetError());
        SDL_DestroySurface(surface);
        SDL_DestroySurface(borrowed);
        FreeImage_Unload(bitmap24);
        return NULL;
    }

    SDL_DestroySurface(borrowed);
    FreeImage_Unload(bitmap24);

    if (options && options->enhancement_enabled) {
        auto_enhance_image(surface, options);
    }

    return surface;
#else
    (void)filename;
    (void)options;
    return NULL;
#endif
}

static SDL_Surface *load_surface_sdl_image(const char *filename, ImageProcessingOptions *options) {
#if defined(HAVE_SDL_IMAGE3)
    SDL_Surface *surface = IMG_Load(filename);
    if (!surface) {
        fprintf(stderr, "Failed to load image with SDL_image: %s (%s)\n", filename, SDL_GetError());
        return NULL;
    }

    if (options && options->enhancement_enabled) {
        auto_enhance_image(surface, options);
    }

    return surface;
#else
    (void)filename;
    (void)options;
    return NULL;
#endif
}

bool image_loader_backend_available(ImageBackend backend) {
    switch (backend) {
        case IMAGE_BACKEND_FREEIMAGE:
#if defined(HAVE_FREEIMAGE)
            return true;
#else
            return false;
#endif
        case IMAGE_BACKEND_SDL_IMAGE:
#if defined(HAVE_SDL_IMAGE3)
            return true;
#else
            return false;
#endif
        case IMAGE_BACKEND_AUTO:
        case IMAGE_BACKEND_COUNT:
        default:
            return false;
    }
}

const char *image_loader_backend_name(ImageBackend backend) {
    switch (backend) {
        case IMAGE_BACKEND_AUTO:
            return "auto";
        case IMAGE_BACKEND_FREEIMAGE:
            return "freeimage";
        case IMAGE_BACKEND_SDL_IMAGE:
            return "sdl_image";
        case IMAGE_BACKEND_COUNT:
        default:
            return "unknown";
    }
}

ImageBackend image_loader_parse_backend(const char *name) {
    if (!name || !name[0]) {
        return IMAGE_BACKEND_AUTO;
    }
    if (strcasecmp(name, "auto") == 0) {
        return IMAGE_BACKEND_AUTO;
    }
    if (strcasecmp(name, "freeimage") == 0) {
        return IMAGE_BACKEND_FREEIMAGE;
    }
    if (strcasecmp(name, "sdl_image") == 0 || strcasecmp(name, "sdl-image") == 0 || strcasecmp(name, "sdlimage") == 0) {
        return IMAGE_BACKEND_SDL_IMAGE;
    }
    return IMAGE_BACKEND_COUNT;
}

bool image_loader_set_preferred_backend(ImageBackend backend) {
    if (backend == IMAGE_BACKEND_COUNT) {
        return false;
    }

    if (backend != IMAGE_BACKEND_AUTO && !image_loader_backend_available(backend)) {
        return false;
    }

    preferred_backend = backend;

    if (loader_initialized) {
        ImageBackend chosen = choose_backend(preferred_backend);
        if (chosen == IMAGE_BACKEND_AUTO) {
            return false;
        }
        active_backend = chosen;
    }

    return true;
}

ImageBackend image_loader_get_active_backend(void) {
    return active_backend;
}

bool image_loader_init(void) {
    if (loader_initialized) {
        return true;
    }

    const char *env_backend = SDL_getenv("IC_IMAGE_BACKEND");
    if (env_backend && env_backend[0]) {
        ImageBackend env_parsed = image_loader_parse_backend(env_backend);
        if (env_parsed != IMAGE_BACKEND_COUNT) {
            preferred_backend = env_parsed;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Ignoring invalid IC_IMAGE_BACKEND='%s' (expected auto|freeimage|sdl_image)",
                        env_backend);
        }
    }

#if defined(HAVE_FREEIMAGE)
    FreeImage_Initialise(FALSE);
    freeimage_initialized = true;
    SDL_Log("FreeImage version: %s", FreeImage_GetVersion());
#endif

    ImageBackend chosen = choose_backend(preferred_backend);
    if (chosen == IMAGE_BACKEND_AUTO) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "No image backend available. Build with FreeImage and/or SDL3_image.");
#if defined(HAVE_FREEIMAGE)
        if (freeimage_initialized) {
            FreeImage_DeInitialise();
            freeimage_initialized = false;
        }
#endif
        return false;
    }

    active_backend = chosen;
    loader_initialized = true;
    SDL_Log("Image loader backend: %s", image_loader_backend_name(active_backend));
    return true;
}

void image_loader_cleanup(void) {
#if defined(HAVE_FREEIMAGE)
    if (freeimage_initialized) {
        FreeImage_DeInitialise();
        freeimage_initialized = false;
    }
#endif

    loader_initialized = false;
    active_backend = IMAGE_BACKEND_AUTO;
}

SDL_Surface *image_load_surface(const char *filename, ImageProcessingOptions *options) {
    if (!filename || !loader_initialized) {
        return NULL;
    }

    // First try active backend.
    SDL_Surface *surface = NULL;
    if (active_backend == IMAGE_BACKEND_FREEIMAGE) {
        surface = load_surface_freeimage(filename, options);
        if (!surface && image_loader_backend_available(IMAGE_BACKEND_SDL_IMAGE)) {
            surface = load_surface_sdl_image(filename, options);
        }
    } else if (active_backend == IMAGE_BACKEND_SDL_IMAGE) {
        surface = load_surface_sdl_image(filename, options);
        if (!surface && image_loader_backend_available(IMAGE_BACKEND_FREEIMAGE)) {
            surface = load_surface_freeimage(filename, options);
        }
    }

    return surface;
}

bool image_is_supported(const char *filename) {
    if (!filename) {
        return false;
    }

    const char *ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    for (int i = 0; supported_extensions[i]; i++) {
        if (strcasecmp(ext, supported_extensions[i]) == 0) {
            return true;
        }
    }

    return false;
}
