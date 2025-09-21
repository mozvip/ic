/**
 * comic_loaders_utils.c
 * Common utility functions for comic file loaders
 */

#define _GNU_SOURCE // Required for strverscmp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h> // For isalnum, if needed for get_filename_from_path or is_image_file
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

/**
 * Compares two image filenames for natural sort order.
 * e.g., "name2.jpg" comes before "name10.jpg".
 */
int image_name_compare(const void *a, const void *b) {
    const char *s1 = *(const char**)a;
    const char *s2 = *(const char**)b;
    return strverscmp(s1, s2);
}

char *dir_name(char *path) {
    if (!path) return NULL;
    char *last_slash = strrchr(path, '/');
    char *last_backslash = strrchr(path, '\\');
    char *last_separator = last_slash > last_backslash ? last_slash : last_backslash;
    if (last_separator) {
        if (last_separator == path) {
            // Path is like "/file" or "\file", return root
            path[1] = '\0';
        } else {
            *last_separator = '\0';
        }
    } else {
        // No separator found, return "."
        strcpy(path, ".");
    }
    return path;
}

bool is_directory(char *path) {
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        // Error accessing the path
        return false;
    }
    return S_ISDIR(path_stat.st_mode);
}

bool is_image_file(const char *filename) {
    if (!filename) return false;
    
    // Get the file extension
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    
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