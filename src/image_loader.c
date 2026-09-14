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

#if defined(HAVE_FREEIMAGE)
// Verify that a JPEG's embedded EXIF (APP1) block is internally consistent:
// the TIFF header must be valid and IFD0's offset + entry table must stay
// within the segment. Returns false only for JPEGs whose EXIF is malformed.
static bool exif_tiff_ifd0_in_bounds(const unsigned char *t, long n) {
    if (n < 8) {
        return false; // no room for even a TIFF header
    }
    bool le;
    if (t[0] == 'I' && t[1] == 'I') {
        le = true;
    } else if (t[0] == 'M' && t[1] == 'M') {
        le = false;
    } else {
        return false; // invalid byte-order marker
    }
    unsigned magic = le ? (unsigned)(t[2] | (t[3] << 8))
                        : (unsigned)((t[2] << 8) | t[3]);
    if (magic != 42) {
        return false; // not TIFF 0x002A
    }
    unsigned long off = le
        ? (unsigned long)(t[4] | (t[5] << 8) | ((unsigned long)t[6] << 16) | ((unsigned long)t[7] << 24))
        : (unsigned long)(((unsigned long)t[4] << 24) | ((unsigned long)t[5] << 16) | (t[6] << 8) | t[7]);
    if (off + 2 > (unsigned long)n) {
        return false; // IFD0 offset points past the segment; can't read count
    }
    unsigned count = le ? (unsigned)(t[off] | (t[off + 1] << 8))
                        : (unsigned)((t[off] << 8) | t[off + 1]);
    // entry count (2) + count * 12-byte entries + next-IFD pointer (4)
    unsigned long need = off + 2 + (unsigned long)count * 12 + 4;
    return need <= (unsigned long)n;
}

// FreeImage 3.18's JPEG plugin (jpeg_read_exif_profile) performs an unchecked
// out-of-bounds read on a truncated/malformed APP1 "Exif" segment, crashing
// the process inside FreeImage_Load. Detect that up front so the caller can
// fall back to a more robust backend instead of dereferencing garbage.
static bool jpeg_exif_is_safe_for_freeimage(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        return true; // let FreeImage report the open failure normally
    }

    unsigned char soi[2];
    if (fread(soi, 1, 2, f) != 2 || soi[0] != 0xFF || soi[1] != 0xD8) {
        fclose(f);
        return true; // not a JPEG -> not affected by this bug
    }

    bool safe = true;
    for (;;) {
        int b = fgetc(f);
        if (b == EOF) {
            break;
        }
        if (b != 0xFF) {
            continue; // resync to the next marker prefix
        }
        int marker;
        do {
            marker = fgetc(f);
        } while (marker == 0xFF);
        if (marker == EOF) {
            break;
        }
        // Standalone markers carry no length/payload.
        if (marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (marker == 0xDA) {
            break; // start of scan: no more metadata markers follow
        }
        unsigned char lb[2];
        if (fread(lb, 1, 2, f) != 2) {
            break;
        }
        long seglen = (lb[0] << 8) | lb[1];
        if (seglen < 2) {
            break; // corrupt length
        }
        long payload = seglen - 2;
        if (marker != 0xE1) {
            fseek(f, payload, SEEK_CUR);
            continue;
        }
        // APP1: may or may not be EXIF.
        unsigned char sig[6];
        long want = (payload < 6) ? payload : 6;
        long got = (long)fread(sig, 1, (size_t)want, f);
        if (got == 6 && memcmp(sig, "Exif\0\0", 6) == 0) {
            long tiff = payload - 6; // declared bytes of TIFF data
            if (tiff <= 0) {
                safe = false;
            } else {
                unsigned char *buf = (unsigned char *)malloc((size_t)tiff);
                if (buf && (long)fread(buf, 1, (size_t)tiff, f) == tiff) {
                    safe = exif_tiff_ifd0_in_bounds(buf, tiff);
                } else {
                    safe = false; // fewer bytes than declared -> truncated
                }
                free(buf);
            }
            break; // found and evaluated the EXIF segment
        }
        // Non-EXIF APP1; skip the remainder of this segment.
        fseek(f, payload - got, SEEK_CUR);
    }

    fclose(f);
    return safe;
}
#endif

static SDL_Surface *load_surface_freeimage(const char *filename, ImageProcessingOptions *options) {
#if defined(HAVE_FREEIMAGE)
    if (!jpeg_exif_is_safe_for_freeimage(filename)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Declining FreeImage for %s: malformed JPEG EXIF would crash FreeImage; "
                    "falling back to another backend",
                    filename);
        return NULL;
    }

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

    if (options && (options->enhancement_enabled || options->color_filter != COLOR_FILTER_NONE)) {
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

    if (options && (options->enhancement_enabled || options->color_filter != COLOR_FILTER_NONE)) {
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

bool image_loader_exif_available(void) {
#if defined(HAVE_FREEIMAGE)
    return true;
#else
    return false;
#endif
}

int image_load_exif(const char *filename, ImageExifEntry *entries, int max_entries) {
#if defined(HAVE_FREEIMAGE)
    if (!filename || !entries || max_entries <= 0) {
        return 0;
    }

    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(filename, 0);
    if (fif == FIF_UNKNOWN) {
        fif = FreeImage_GetFIFFromFilename(filename);
    }
    if (fif == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fif)) {
        return 0;
    }

    // Load header/metadata only when the format allows it, else fall back to a full decode.
    FIBITMAP *dib = FreeImage_Load(fif, filename, FIF_LOAD_NOPIXELS);
    if (!dib) {
        dib = FreeImage_Load(fif, filename, 0);
    }
    if (!dib) {
        return 0;
    }

    static const FREE_IMAGE_MDMODEL models[] = {
        FIMD_EXIF_MAIN,
        FIMD_EXIF_EXIF,
        FIMD_EXIF_GPS,
    };

    int count = 0;
    for (size_t m = 0; m < sizeof(models) / sizeof(models[0]) && count < max_entries; m++) {
        FITAG *tag = NULL;
        FIMETADATA *mdhandle = FreeImage_FindFirstMetadata(models[m], dib, &tag);
        if (!mdhandle) {
            continue;
        }
        do {
            const char *key = FreeImage_GetTagKey(tag);
            // FreeImage_TagToString returns a pointer to an internal static buffer,
            // so copy the value immediately before the next call overwrites it.
            const char *val = FreeImage_TagToString(models[m], tag, NULL);
            if (key && val && val[0] != '\0') {
                strncpy(entries[count].key, key, IMAGE_EXIF_KEY_LEN - 1);
                entries[count].key[IMAGE_EXIF_KEY_LEN - 1] = '\0';
                strncpy(entries[count].value, val, IMAGE_EXIF_VALUE_LEN - 1);
                entries[count].value[IMAGE_EXIF_VALUE_LEN - 1] = '\0';
                count++;
            }
        } while (count < max_entries && FreeImage_FindNextMetadata(mdhandle, &tag));
        FreeImage_FindCloseMetadata(mdhandle);
    }

    FreeImage_Unload(dib);
    return count;
#else
    (void)filename;
    (void)entries;
    (void)max_entries;
    return 0;
#endif
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
