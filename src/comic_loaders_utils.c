/**
 * comic_loaders_utils.c
 * Common utility functions for comic file loaders
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "comic_loaders.h"

// Helper function to get filename from a path (last component after any slashes)
const char* get_filename_from_path(const char* path) {
    const char* last_slash = strrchr(path, '/');
    const char* last_backslash = strrchr(path, '\\');
    
    // Get the last separator (either slash or backslash)
    const char* last_separator = last_slash > last_backslash ? last_slash : last_backslash;
    
    // If no separator found, the path is already just a filename
    if (last_separator == NULL) {
        return path;
    }
    
    // Return the part after the separator
    return last_separator + 1;
}

// Function to compare image filenames for sorting
int image_name_compare(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

bool is_image_file(const char *filename) {
    if (!filename) return false;
    
    // Get the file extension
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    
    // Check if it's one of our supported image formats
    ext++; // Skip the '.'
    return (strcasecmp(ext, "jpg") == 0 ||
            strcasecmp(ext, "jpeg") == 0 ||
            strcasecmp(ext, "png") == 0 ||
            strcasecmp(ext, "gif") == 0 ||
            strcasecmp(ext, "bmp") == 0 ||
            strcasecmp(ext, "webp") == 0 ||
            strcasecmp(ext, "JPG") == 0 ||
            strcasecmp(ext, "JPEG") == 0 ||
            strcasecmp(ext, "PNG") == 0 ||
            strcasecmp(ext, "GIF") == 0 ||
            strcasecmp(ext, "BMP") == 0 ||
            strcasecmp(ext, "WEBP") == 0);
}