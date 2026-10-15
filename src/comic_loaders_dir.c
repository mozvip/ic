/**
 * comic_loaders_dir.c
 * Implementation of directory-based comic loading
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "comic_loaders.h"

// External functions from comic_loaders_utils.c
extern int image_name_compare(const void *a, const void *b);
extern const char* get_filename_from_path(const char* path);

bool load_directory(const char *path, ImageEntry *images, int *image_count, int max_images, ProgressCallback progress_cb) {
    if (progress_cb) {
        progress_cb(0.0f, "Scanning directory...");
    }
    
    DIR *dir;
    struct dirent *entry;
    char *image_paths[max_images];
    int count = 0;
    
    // Open the directory
    dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "Failed to open directory: %s\n", path);
        return false;
    }
    
    size_t total_entries = 0;
    while (readdir(dir) != NULL) total_entries++;
    rewinddir(dir);
    if (progress_cb) progress_cb(0.1f, "Collecting image paths...");
    size_t scanned_entries = 0;
    while ((entry = readdir(dir)) != NULL && count < max_images) {
        scanned_entries++;
        if (entry->d_type == DT_REG && is_image_file(entry->d_name)) {
            char *full_path = malloc(strlen(path) + strlen(entry->d_name) + 2); // +2 for '/' and null terminator
            if (!full_path) continue;
            
            sprintf(full_path, "%s/%s", path, entry->d_name);
            image_paths[count++] = full_path;
            
        }
        if (progress_cb) {
            float fraction = total_entries ? (float)scanned_entries / total_entries : 1.0f;
            if (fraction > 1.0f) fraction = 1.0f;
            char msg[256];
            snprintf(msg, sizeof(msg), "Scanning directory (%zu/%zu entries, %d images)...",
                     scanned_entries, total_entries, count);
            progress_cb(0.1f + 0.55f * fraction, msg);
        }
    }
    
    closedir(dir);
    
    if (count == 0) {
        if (progress_cb) {
            progress_cb(1.0f, "No images found");
        }
        return false;
    }
    
    if (progress_cb) {
        progress_cb(0.7f, "Sorting images...");
    }
    
    // Sort images by name
    qsort(image_paths, count, sizeof(char *), image_name_compare);
    
    if (progress_cb) {
        progress_cb(0.8f, "Preparing image data...");
    }
    
    // Store sorted images
    for (int i = 0; i < count; i++) {
        images[i].path = image_paths[i];
        images[i].surface = NULL;  // Initialize surface to NULL
        if (progress_cb) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Preparing image (%d/%d)...", i + 1, count);
            progress_cb(0.8f + 0.18f * (float)(i + 1) / count, msg);
        }
    }
    
    *image_count = count;
    
    if (progress_cb) {
        progress_cb(1.0f, "Loading complete");
    }
    
    return count > 0;
}