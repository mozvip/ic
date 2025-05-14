/**
 * comic_loaders_pdf.c
 * Implementation of PDF comic loading using pdfimages command-line tool
 * Simple version without threading - loads pages sequentially
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include "comic_loaders.h"

// External functions from comic_loaders_utils.c
extern int image_name_compare(const void *a, const void *b);
extern const char* get_filename_from_path(const char* path);

const int PDF_PIXEL_DENSITY = 120; // DPI for PDF rendering

// Get PDF page count using pdfinfo (part of poppler-utils)
static int get_pdf_page_count(const char *path) {
    char *escaped_path = escape_shell_arg(path);
    char cmd[1024];
    
    // Use pdfinfo to get page count
    snprintf(cmd, sizeof(cmd), "pdfinfo %s | grep Pages | awk '{print $2}'", escaped_path);
    
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        free(escaped_path);
        return -1;
    }
    
    char line[128];
    int n_pages = 0;
    
    if (fgets(line, sizeof(line), fp)) {
        sscanf(line, "%d", &n_pages);
    }
    
    pclose(fp);
    free(escaped_path);
    
    return n_pages;
}

// Extract a single page from PDF using pdfimages
static char* extract_pdf_page(const char *pdf_path, int page_index, const char *output_prefix, const char *expected_path, bool *success) {
    char *escaped_pdf_path = escape_shell_arg(pdf_path);
    char *escaped_output_prefix = escape_shell_arg(output_prefix);

    char cmd[2048];

    // old version
    // snprintf(cmd, sizeof(cmd),
    //          "pdfimages -all -f %d -l %d \"%s\" %s",
    //          page_index + 1, page_index + 1,
    //          pdf_path, output_prefix);
    // new version
    // Create command to render a single page using pdftoppm
    snprintf(cmd, sizeof(cmd), 
             "pdftoppm -r %d -f %d -l %d -jpeg \"%s\" %s",
             PDF_PIXEL_DENSITY, page_index + 1, page_index + 1,
             pdf_path, output_prefix);

    // Execute the command
    int result = system(cmd);
    
    free(escaped_pdf_path);
    free(escaped_output_prefix);
    
    // Check if extraction was successful
    *success = (result == 0);
    
    if (!*success) {
        return NULL;
    }
   
    // Check if the file exists
    if (access(expected_path, F_OK) != 0) {
        *success = false;
        return NULL;
    }

    return strdup(expected_path);
}

ArchiveHandle* pdf_open(const char *path, int *total_images, ProgressCallback progress_cb) {
    if (progress_cb) {
        progress_cb(0.0f, "Opening PDF document...");
    }    
   
    if (progress_cb) {
        progress_cb(0.1f, "Reading PDF document info...");
    }
    
    // Create temp directory for images
    char temp_dir[256];
    snprintf(temp_dir, sizeof(temp_dir), "/tmp/ic_viewer_pdf_XXXXXX");
    if (mkdtemp(temp_dir) == NULL) {
        fprintf(stderr, "Failed to create temporary directory\n");
        if (progress_cb) {
            progress_cb(1.0f, "Failed to create temporary directory");
        }
        return NULL;
    }
    
    if (progress_cb) {
        progress_cb(0.2f, "Getting page count from the PDF...");
    }
    // Get number of pages using pdfinfo
    int n_pages = get_pdf_page_count(path);
    
    if (n_pages <= 0) {
        fprintf(stderr, "PDF document has no pages or could not determine page count\n");
        if (progress_cb) {
            progress_cb(1.0f, "PDF document has no pages or could not determine page count");
        }
        return NULL;
    }
    
    if (progress_cb) {
        progress_cb(0.6f, "Allocating memory for page indices...");
    }

    // Allocate handle
    ArchiveHandle *handle = (ArchiveHandle*)malloc(sizeof(ArchiveHandle));
    if (!handle) {
        if (progress_cb) {
            progress_cb(1.0f, "Memory allocation failed");
        }
        return NULL;
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
    snprintf(expected_path, sizeof(expected_path), "%s-%02d.jpg", output_prefix, page_index + 1);
    
    // Check if the file already exists
    if (access(expected_path, F_OK) == 0) {
        *out_path = strdup(expected_path);        
        return true;
    }
    
    // Render the page
    bool success = false;
    char *result_path = extract_pdf_page(handle->path, page_index, output_prefix, expected_path, &success);
    
    if (!success || result_path == NULL) {
        fprintf(stderr, "Failed to render page %d\n", page_index + 1);
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
        // char cmd[512];
        // snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", handle->temp_dir);
        // system(cmd);
        free(handle->temp_dir);
    }
    
    free(handle->path);
    free(handle);
}