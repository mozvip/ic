/**
 * comic_loaders.c
 * Implementation of common comic file loading functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zip.h>
#include "comic_loaders.h"

// External functions from comic_loaders_utils.c
extern const char* get_filename_from_path(const char* path);
extern bool is_image_file(const char *filename);

// Common functions for on-demand loading
ArchiveHandle* archive_open(const char *path, ArchiveType type, int *total_images, ProgressCallback progress_cb) {
    if (!path || !total_images) {
        return NULL;
    }
    
    ArchiveHandle *handle = NULL;
    
    switch (type) {
        case ARCHIVE_TYPE_CBZ:
            handle = cbz_open(path, total_images, progress_cb);
            break;
        case ARCHIVE_TYPE_CBR:
            handle = cbr_open(path, total_images, progress_cb);
            break;
        case ARCHIVE_TYPE_PDF:
            handle = pdf_open(path, total_images, progress_cb);
            break;
        default:
            fprintf(stderr, "Unsupported archive type\n");
            return NULL;
    }
    
    return handle;
}

bool archive_get_image(ArchiveHandle *handle, int index, char **out_path) {
    if (!handle || !out_path || index < 0 || index >= handle->total_images) {
        return false;
    }
    
    switch (handle->type) {
        case ARCHIVE_TYPE_CBZ:
            return cbz_get_image(handle, index, out_path);
        case ARCHIVE_TYPE_CBR:
            return cbr_get_image(handle, index, out_path);
        case ARCHIVE_TYPE_PDF:
            return pdf_get_image(handle, index, out_path);
        default:
            return false;
    }
}

void archive_close(ArchiveHandle *handle) {
    if (!handle) {
        return;
    }
    
    switch (handle->type) {
        case ARCHIVE_TYPE_CBZ:
            cbz_close(handle);
            break;
        case ARCHIVE_TYPE_CBR:
            cbr_close(handle);
            break;
        case ARCHIVE_TYPE_PDF:
            pdf_close(handle);
            break;
        default:
            break;
    }
}

// Helper function to escape a string for shell command
char* escape_shell_arg(const char *str) {
    if (str == NULL) return NULL;
    
    size_t len = strlen(str);
    char *escaped = (char*)malloc((2*len + 3) * sizeof(char)); // Worst case: each char needs escaping + quotes
    
    if (escaped == NULL) return NULL;
    
    escaped[0] = '\''; // Single quote at start
    
    size_t j = 1;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\'') {
            // For single quotes, we need to close the quoted string,
            // add an escaped quote, and then reopen the quoted string
            escaped[j++] = '\''; // Close quote
            escaped[j++] = '\\';
            escaped[j++] = '\''; // Escaped quote
            escaped[j++] = '\''; // Reopen quote
        } else {
            escaped[j++] = str[i];
        }
    }
    
    escaped[j++] = '\''; // Single quote at end
    escaped[j] = '\0';
    
    return escaped;
}


