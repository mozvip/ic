/**
 * comic_loaders_pdf.c
 * Implementation of PDF comic loading using pdftoppm and pdfinfo command-line tools
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include "comic_loaders.h"
#include "process_utils.h"

// External functions from comic_loaders_utils.c
extern int image_name_compare(const void *a, const void *b);
extern const char* get_filename_from_path(const char* path);

const int PDF_PIXEL_DENSITY = 120; // DPI for PDF rendering

// Get PDF page count using pdfinfo (part of poppler-utils)
static int get_pdf_page_count(const char *path) {
    const char *pdfinfo_args[] = {"pdfinfo", path, NULL};
    char *pdfinfo_output = execute_command_with_output(pdfinfo_args);
    if (pdfinfo_output == NULL) {
        return -1;
    }

    char *line = strtok(pdfinfo_output, "\n");
    int n_pages = 0;
    while (line != NULL) {
        if (strncmp(line, "Pages:", 6) == 0) {
            sscanf(line + 6, "%d", &n_pages);
            break;
        }
        line = strtok(NULL, "\n");
    }

    free(pdfinfo_output);
    return n_pages;
}

// Extract a single page from PDF using pdfimages
static char* extract_pdf_page(const char *pdf_path, int page_index, const char *output_prefix, const char *expected_path, bool *success) {
    char page_str[12];
    char density_str[12];
    snprintf(page_str, sizeof(page_str), "%d", page_index + 1);
    snprintf(density_str, sizeof(density_str), "%d", PDF_PIXEL_DENSITY);

    const char *args[] = {
        "pdftoppm",
        "-r", density_str,
        "-f", page_str,
        "-l", page_str,
        "-jpeg",
        pdf_path,
        output_prefix,
        NULL
    };

    // Execute the command
    execute_command(args);
   
    // Check if the file exists
    if (access(expected_path, F_OK) != 0) {
        *success = false;
        return NULL;
    }

    *success = true;
    return strdup(expected_path);
}

ArchiveHandle* pdf_open(const char *path, int *total_images, ProgressCallback progress_cb) {
    if (progress_cb) {
        progress_cb(0.0f, "Creating temp folder...");
    }    
    
    // Create temp directory for images
    char temp_dir[256];
    snprintf(temp_dir, sizeof(temp_dir), "/tmp/ic_viewer_pdf_XXXXXX");
    if (mkdtemp(temp_dir) == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create temporary directory");
        if (progress_cb) {
            progress_cb(1.0f, "Failed to create temporary directory");
        }
        return NULL;
    }
    
    if (progress_cb) {
        progress_cb(0.2f, "Getting page count...");
    }
    // Get number of pages using pdfinfo
    int n_pages = get_pdf_page_count(path);
    
    if (n_pages <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PDF document has no pages or could not determine page count");
        if (progress_cb) {
            progress_cb(1.0f, "PDF document has no pages or could not determine page count");
        }
        return NULL;
    }
    
    if (progress_cb) {
        progress_cb(0.4f, "Allocating memory for page indices...");
    }

    // Allocate handle
    ArchiveHandle *handle = (ArchiveHandle*)malloc(sizeof(ArchiveHandle));
    if (!handle) {
        if (progress_cb) {
            progress_cb(1.0f, "Memory allocation failed");
        }
        return NULL;
    }

    if (progress_cb) {
        progress_cb(0.6f, "Initializing memory...");
    }    
    
    // Initialize handle
    handle->type = ARCHIVE_TYPE_PDF;
    handle->path = strdup(path);
    handle->archive_ptr = NULL;
    handle->temp_dir = strdup(temp_dir);
    handle->total_images = n_pages;
    handle->entry_names = NULL;
    
    // Set up page indices (1 to 1 mapping for PDF)
    handle->page_indices = (int*)malloc(n_pages * sizeof(int));
    for (int i = 0; i < n_pages; i++) {
        handle->page_indices[i] = i;
    }
    
    *total_images = n_pages;
    
    if (progress_cb) {
        char msg[256];
        snprintf(msg, sizeof(msg), "PDF loaded with %d pages", n_pages);
        progress_cb(1.0f, msg);
    }
    
    return handle;
}

bool pdf_get_image(ArchiveHandle *handle, int index, char **out_path) {
    if (!handle || !out_path || index < 0 || index >= handle->total_images) {
        return false;
    }
    
    int page_index = handle->page_indices[index];
    
    // Check for existing rendered file for this page
    char output_prefix[512];
    snprintf(output_prefix, sizeof(output_prefix), "%s/page", handle->temp_dir);
    
    char expected_path[512];

    // number of digits depends on total_images
    int num_digits = handle->total_images > 99 ? 3 : handle->total_images > 9 ? 2 : 1;

     /* Build the numbered filename safely: format the number into a small buffer first,
         then compose the final path to avoid %0*d format-truncation warnings. */
     char numbuf[32];
     snprintf(numbuf, sizeof(numbuf), "%0*d", num_digits, page_index + 1);
    /* Compose expected_path safely to avoid format-truncation warnings. */
    expected_path[0] = '\0';
    strncpy(expected_path, output_prefix, sizeof(expected_path) - 1);
    expected_path[sizeof(expected_path) - 1] = '\0';
    strncat(expected_path, "-", sizeof(expected_path) - strlen(expected_path) - 1);
    strncat(expected_path, numbuf, sizeof(expected_path) - strlen(expected_path) - 1);
    strncat(expected_path, ".jpg", sizeof(expected_path) - strlen(expected_path) - 1);

    // Check if the file already exists
    if (access(expected_path, F_OK) == 0) {
        *out_path = strdup(expected_path);        
        return true;
    }
    
    // Render the page
    bool success = false;
    char *result_path = extract_pdf_page(handle->path, page_index, output_prefix, expected_path, &success);
    
    if (!success || result_path == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to render page %d", page_index + 1);
        if (result_path) {
            free(result_path);
        }
        
        return false;
    }
    
    *out_path = result_path;
    
    return true;
}

void pdf_close(ArchiveHandle *handle) {
    if (!handle) {
        return;
    }
    
    // Free page indices
    free(handle->page_indices);
    
    // Clean up temporary directory (if needed)
    if (handle->temp_dir) {
        // Optionally remove temp files
        // const char *args[] = {"rm", "-rf", handle->temp_dir, NULL};
        // execute_command(args);
        free(handle->temp_dir);
    }
    
    free(handle->path);
    free(handle);
}