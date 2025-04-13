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
    
    // First pass: collect image paths
    while ((entry = readdir(dir)) != NULL && count < max_images) {
        if (entry->d_type == DT_REG && is_image_file(entry->d_name)) {
            char *full_path = malloc(strlen(path) + strlen(entry->d_name) + 2); // +2 for '/' and null terminator
            if (!full_path) continue;
            
            sprintf(full_path, "%s/%s", path, entry->d_name);
            image_paths[count++] = full_path;
            
            // Update progress while scanning
            if (progress_cb && count % 5 == 0) {  // Update every 5 images to avoid too frequent updates
                char msg[256];
                snprintf(msg, sizeof(msg), "Found %d images...", count);
                progress_cb(0.5f, msg);  // Use 0.5 as we're in the scanning phase
            }
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
        progress_cb(0.9f, "Preparing image data...");
    }
    
    // Store sorted images
    for (int i = 0; i < count; i++) {
        images[i].path = image_paths[i];
        images[i].surface = NULL;  // Initialize surface to NULL
        images[i].texture = NULL;
    }
    
    *image_count = count;
    
    if (progress_cb) {
        progress_cb(1.0f, "Loading complete");
    }
    
    return count > 0;
}