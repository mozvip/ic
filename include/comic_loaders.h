/**
 * comic_loaders.h
 * Header file for comic file loading functions
 */

#ifndef COMIC_LOADERS_H
#define COMIC_LOADERS_H

#include <stdbool.h>
#include <SDL2/SDL.h>

// Include comic viewer types
#include "comic_viewer.h"

// Image entry structure used by loaders
typedef struct {
    char *path;               // Path to the image
    SDL_Surface *surface;     // Loaded surface
    SDL_Texture *texture;     // Loaded texture
    int width;                // Original image width
    int height;               // Original image height
} ImageEntry;

// Progress callback function type definition
typedef void (*ProgressCallback)(float progress, const char *message);

// Archive handle for on-demand loading
typedef enum {
    ARCHIVE_TYPE_NONE,
    ARCHIVE_TYPE_CBZ,
    ARCHIVE_TYPE_CBR,
    ARCHIVE_TYPE_PDF
} ArchiveType;

// Complete definition of ArchiveHandle structure
typedef struct ArchiveHandle {
    ArchiveType type;           // Type of archive
    char *path;                 // Path to the archive file
    int total_images;           // Total number of images in the archive
    char *temp_dir;             // Temporary directory for extracted files
    void *archive_ptr;          // Pointer to the archive-specific handle
    char **entry_names;         // Array of entry names (for CBZ/CBR)
    int *page_indices;          // Array of page indices (for PDF)
} ArchiveHandle;

// Function to check if a file is a supported image
bool is_image_file(const char *filename);

// Load images from a directory
bool load_directory(const char *path, ImageEntry *images, int *image_count, int max_images, ProgressCallback progress_cb);

// On-demand loading interface
// Open an archive and prepare for on-demand loading
ArchiveHandle* archive_open(const char *path, ArchiveType type, int *total_images, ProgressCallback progress_cb);

// Get an image from the archive at the given index
bool archive_get_image(ArchiveHandle *handle, int index, char **out_path);

// Close an archive handle and free resources
void archive_close(ArchiveHandle *handle);

// Extract a file from an archive to a temporary location
bool extract_temp_file(const char *archive_path, const char *entry_name, const char *temp_path);

// Escape a string for shell argument
char* escape_shell_arg(const char *str);

// CBZ specific functions for on-demand loading
ArchiveHandle* cbz_open(const char *path, int *total_images, ProgressCallback progress_cb);
bool cbz_get_image(ArchiveHandle *handle, int index, char **out_path);
void cbz_close(ArchiveHandle *handle);

// CBR specific functions for on-demand loading
ArchiveHandle* cbr_open(const char *path, int *total_images, ProgressCallback progress_cb);
bool cbr_get_image(ArchiveHandle *handle, int index, char **out_path);
void cbr_close(ArchiveHandle *handle);

// PDF specific functions for on-demand loading
ArchiveHandle* pdf_open(const char *path, int *total_images, ProgressCallback progress_cb);
bool pdf_get_image(ArchiveHandle *handle, int index, char **out_path);
void pdf_close(ArchiveHandle *handle);

#endif // COMIC_LOADERS_H