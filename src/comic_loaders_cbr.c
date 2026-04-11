/**
 * comic_loaders_cbr.c
 * Implementation of CBR/RAR comic loading using unrar command-line tool
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "comic_loaders.h"
#include "process_utils.h"
#include "temp_utils.h"

// External functions from comic_loaders_utils.c
extern int image_name_compare(const void *a, const void *b);
extern const char* get_filename_from_path(const char* path);

// Forward declaration of the ArchiveHandle struct defined in comic_loaders.c
typedef struct ArchiveHandle ArchiveHandle;

// Forward declarations of CBR functions
void cbr_close(ArchiveHandle *handle);

// Helper function to check if unrar is installed
static bool check_unrar_available() {
    const char *args[] = {"which", "unrar", NULL};
    char *output = NULL;
    int rc = execute_process(args, true, &output);
    bool found = (rc == 0 && output != NULL && strlen(output) > 0);
    if (output) SDL_free(output);
    return found;
}

ArchiveHandle* cbr_open(const char *path, int *total_images, ProgressCallback progress_cb) {
    if (progress_cb) {
        progress_cb(0.0f, "Checking RAR support...");
    }
    
    // Check if unrar is available
    if (!check_unrar_available()) {
        fprintf(stderr, "unrar command not found. Please install unrar.\n");
        if (progress_cb) {
            progress_cb(1.0f, "unrar command not found. Please install unrar.");
        }
        return NULL;
    }
    
    if (progress_cb) {
        progress_cb(0.2f, "Creating temp folder...");
    }

    // Create a temporary directory for extraction
    char temp_dir[512];
    if (!temp_utils_create_dir(temp_dir, sizeof(temp_dir), "ic_viewer_XXXXXX")) {
        return NULL;
    }
    
    if (progress_cb) {
        progress_cb(0.3f, "Prepare memory...");
    }
    
    // Allocate handle
    ArchiveHandle *handle = (ArchiveHandle*)malloc(sizeof(ArchiveHandle));
    if (!handle) {
        return NULL;
    }
    
    // Initialize handle
    handle->type = ARCHIVE_TYPE_CBR;
    handle->path = strdup(path);
    handle->temp_dir = strdup(temp_dir);
    handle->total_images = 0;
    handle->archive_ptr = NULL;  // Not needed for command-line approach
    handle->entry_names = NULL;
    handle->page_indices = NULL;
    
    if (progress_cb) {
        progress_cb(0.5f, "Analyzing archive...");
    }

    // Use unrar command to list files in the archive
    const char *args[] = {"unrar", "lb", path, NULL};
    
    char *output = NULL;
    if (execute_process(args, true, &output) != 0 || output == NULL) {
        fprintf(stderr, "Failed to run unrar command to list files\n");
        cbr_close(handle);
        return NULL;
    }
    
    if (progress_cb) {
        progress_cb(0.8f, "Parsing rar output...");
    }

    // Read list of files from unrar output
    char *line = strtok(output, "\n");
    int capacity = 100;  // Initial capacity
    char **image_entries = (char**)malloc(capacity * sizeof(char*));
    int count = 0;
    
    if (!image_entries) {
        fprintf(stderr, "Memory allocation failed\n");
                    SDL_free(output);
        cbr_close(handle);
        return NULL;
    }

    if (progress_cb) {
        progress_cb(0.9f, "Creating list of images...");
    }    
    
    while (line != NULL) {
        // Get the actual filename (last part of path)
        const char* filename = get_filename_from_path(line);
        
        // Check if it's an image file
        if (is_image_file(filename)) {
            // Resize array if needed
            if (count >= capacity) {
                capacity *= 2;
                char **new_entries = (char**)realloc(image_entries, capacity * sizeof(char*));
                if (!new_entries) {
                    fprintf(stderr, "Memory allocation failed\n");
                    for (int i = 0; i < count; i++) {
                        free(image_entries[i]);
                    }
                    free(image_entries);
                    SDL_free(output);
                    cbr_close(handle);
                    return NULL;
                }
                image_entries = new_entries;
            }
            
            // Store entry name
            image_entries[count++] = strdup(line);
        }
        line = strtok(NULL, "\n");
    }
    
    SDL_free(output);
    
    if (count == 0) {
        fprintf(stderr, "No images found in RAR archive\n");
        free(image_entries);
        cbr_close(handle);
        return NULL;
    }
    
    if (progress_cb) {
        progress_cb(0.95f, "Sorting images...");
    }
    
    // Sort entries by name
    qsort(image_entries, count, sizeof(char*), image_name_compare);
    
    // Store the count and entries in the handle
    handle->total_images = count;
    handle->entry_names = image_entries;
    
    *total_images = count;
    
    if (progress_cb) {
        progress_cb(1.0f, "Archive ready");
    }
    
    return handle;
}

bool cbr_get_image(ArchiveHandle *handle, int index, char **out_path) {
    if (!handle || !out_path || index < 0 || index >= handle->total_images) {
        return false;
    }
    
    const char *entry_name = handle->entry_names[index];

    // Construct the output path
    char output_path[512];
    snprintf(output_path, sizeof(output_path), "%s/%s", handle->temp_dir, entry_name);
    
    // Check if the file already exists
    if (access(output_path, F_OK) == 0) {
        // File already exists, no need to extract again
        *out_path = strdup(output_path);
        
        return true;
    }
    
    // Create subdirectories if needed
    char *dir_part = strdup(output_path);
    char *last_slash = strrchr(dir_part, '/');
    if (last_slash) {
        *last_slash = '\0';
        const char *args[] = {"mkdir", "-p", dir_part, NULL};
        execute_process(args, false, NULL);
    }
    free(dir_part);
    
    // Extract only the requested file using unrar command
    const char *args[] = {"unrar", "x", "-o+", handle->path, entry_name, handle->temp_dir, NULL};
    
    int extract_result = execute_process(args, false, NULL);
    
    if (extract_result != 0) {
        fprintf(stderr, "Failed to extract file from RAR archive: %s\n", entry_name);       
        return false;
    }
    
    // Check if the file exists after extraction
    if (access(output_path, F_OK) != 0) {
        fprintf(stderr, "File not found after extraction: %s\n", entry_name);
        
        return false;
    }
    
    *out_path = strdup(output_path);
    
    return true;
}

void cbr_close(ArchiveHandle *handle) {
    if (!handle) {
        return;
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
        // Optionally remove temp files
        // const char *args[] = {"rm", "-rf", handle->temp_dir, NULL};
        // execute_process(args, false, NULL);
        free(handle->temp_dir);
    }
    
    free(handle->path);
    free(handle);
}