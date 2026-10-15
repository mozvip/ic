/**
 * comic_loaders_cbz.c
 * Implementation of CBZ/ZIP comic loading
 */

#include "comic_loaders.h"
#include "image_loader.h"
#include "progress_bar.h"
#include "process_utils.h"
#include "temp_utils.h"
#include <SDL3/SDL.h>
#include <zip.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

// External functions from comic_loaders_utils.c
extern int image_name_compare(const void *a, const void *b);
extern const char* get_filename_from_path(const char* path);

// External functions from comic_loaders_utils.c
extern int image_name_compare(const void *a, const void *b);
extern const char* get_filename_from_path(const char* path);

// Forward declaration of the ArchiveHandle struct defined in comic_loaders.c
typedef struct ArchiveHandle ArchiveHandle;

ArchiveHandle* cbz_open(const char *path, int *total_images, ProgressCallback progress_cb) {
    if (progress_cb) {
        progress_cb(0.0f, "Opening ZIP archive...");
    }
    
    struct zip *zip_file;
    int err;
    
    // Open the zip file
    zip_file = zip_open(path, 0, &err);
    if (!zip_file) {
        fprintf(stderr, "Failed to open zip file: %s (error %d)\n", path, err);
        return NULL;
    }
    
    // Get the number of entries
    zip_int64_t num_entries = zip_get_num_entries(zip_file, 0);
    if (num_entries <= 0) {
        zip_close(zip_file);
        return NULL;
    }
    
    if (progress_cb) {
        progress_cb(0.1f, "Creating temporary directory...");
    }
    
    // Create a temporary directory for extraction
    char temp_dir[512];
    if (!temp_utils_create_dir(temp_dir, sizeof(temp_dir), "ic_viewer_XXXXXX")) {
        zip_close(zip_file);
        return NULL;
    }
    
    // Scan for images first to count them
    if (progress_cb) {
        progress_cb(0.2f, "Scanning archive for images...");
    }
    
    // Allocate handle
    ArchiveHandle *handle = (ArchiveHandle*)malloc(sizeof(ArchiveHandle));
    if (!handle) {
        zip_close(zip_file);
        return NULL;
    }
    
    // Initialize handle
    handle->type = ARCHIVE_TYPE_CBZ;
    handle->path = strdup(path);
    handle->archive_ptr = zip_file;
    handle->temp_dir = strdup(temp_dir);
    handle->total_images = 0;
    handle->entry_names = NULL;
    handle->page_indices = NULL;
    // Initialize archive mutex for thread-safe access
    handle->archive_mutex = SDL_CreateMutex();
    if (!handle->archive_mutex) {
        cbz_close(handle);
        return NULL;
    }
    
    // First pass - count image files and collect names
    char **image_entries = (char**)malloc(num_entries * sizeof(char*));
    if (!image_entries) {
        cbz_close(handle);
        return NULL;
    }
    int count = 0;
    
    for (zip_int64_t i = 0; i < num_entries; i++) {
        const char *name = zip_get_name(zip_file, i, 0);
        if (name) {
            // Extract the actual filename (last part of path)
            const char *filename = get_filename_from_path(name);
            
            // Check if it's an image file
            if (is_image_file(filename)) {
                image_entries[count++] = strdup(name);
            }
        }
        
        // Update progress
        if (progress_cb) {
            float progress = 0.2f + (0.6f * (float)(i + 1) / num_entries);
            char msg[256];
            snprintf(msg, sizeof(msg), "Scanning archive (%lld/%lld)...",
                     (long long)(i + 1), (long long)num_entries);
            progress_cb(progress, msg);
        }
    }
    
    if (count == 0) {
        fprintf(stderr, "No images found in archive\n");
        free(image_entries);
        cbz_close(handle);
        return NULL;
    }
    
    // Sort entries by name
    if (progress_cb) progress_cb(0.85f, "Sorting archive pages...");
    qsort(image_entries, count, sizeof(char *), image_name_compare);
    
    // Store the count and entries in the handle
    handle->total_images = count;
    handle->entry_names = image_entries;
    
    *total_images = count;
    if (progress_cb) progress_cb(0.95f, "Preparing archive index...");
    
    if (progress_cb) {
        progress_cb(1.0f, "Archive ready.");
    }
    
    return handle;
}

bool cbz_get_image(ArchiveHandle *handle, int index, char **out_path) {
    if (!handle || !out_path || index < 0 || index >= handle->total_images) {
        return false;
    }
    
    // Get the ZIP handle from the archive handle
    struct zip *zip_archive = (struct zip*)handle->archive_ptr;
    const char *entry_name = handle->entry_names[index];
     
    char output_path[512];
    const char *extension = strrchr(get_filename_from_path(entry_name), '.');
    int path_length = snprintf(output_path, sizeof(output_path), "%s/page_%d%s",
                               handle->temp_dir, index, extension ? extension : "");
    if (path_length < 0 || (size_t)path_length >= sizeof(output_path)) {
        return false;
    }

    SDL_LockMutex(handle->archive_mutex);
    if (access(output_path, F_OK) == 0) {
        *out_path = strdup(output_path);
        SDL_UnlockMutex(handle->archive_mutex);
        return *out_path != NULL;
    }

    struct zip_file *zip_file = zip_fopen(zip_archive, entry_name, 0);
    if (!zip_file) {
        fprintf(stderr, "Failed to open file in ZIP archive: %s\n", entry_name);
        SDL_UnlockMutex(handle->archive_mutex);
        return false;
    }
    
    // Create the output file
    FILE *out_file = fopen(output_path, "wb");
    if (!out_file) {
        fprintf(stderr, "Failed to create output file: %s\n", output_path);
        zip_fclose(zip_file);
        SDL_UnlockMutex(handle->archive_mutex);
        return false;
    }
    
    // Copy data from zip to file
    char buffer[8192];
    zip_int64_t bytes_read;
    bool ok = true;
    
    while ((bytes_read = zip_fread(zip_file, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, (size_t)bytes_read, out_file) != (size_t)bytes_read) {
            ok = false;
            break;
        }
    }

    if (bytes_read < 0) ok = false;
    if (fclose(out_file) != 0) ok = false;
    if (zip_fclose(zip_file) != 0) ok = false;
    if (ok) {
        *out_path = strdup(output_path);
        ok = *out_path != NULL;
    } else {
        remove(output_path);
    }

    SDL_UnlockMutex(handle->archive_mutex);
    return ok;
}

void cbz_close(ArchiveHandle *handle) {
    if (!handle) {
        return;
    }
    
    // Close the zip file
    if (handle->archive_ptr) {
        struct zip *zip_file = (struct zip*)handle->archive_ptr;
        zip_close(zip_file);
    }

    // Destroy archive mutex
    if (handle->archive_mutex) {
        SDL_DestroyMutex(handle->archive_mutex);
        handle->archive_mutex = NULL;
    }
    
    // Free entry names
    if (handle->entry_names) {
        for (int i = 0; i < handle->total_images; i++) {
            free(handle->entry_names[i]);
        }
        free(handle->entry_names);
    }
    
    // Clean up temporary directory (if needed)
    if (handle->temp_dir) {
        temp_utils_remove_dir(handle->temp_dir);
        free(handle->temp_dir);
    }
    
    free(handle->path);
    free(handle);
}