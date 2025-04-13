/**
 * comic_viewer.h
 * Header file for the comic viewer implementation
 */

#ifndef COMIC_VIEWER_H
#define COMIC_VIEWER_H

#include <stdbool.h>

// Source type enumeration
typedef enum {
    SOURCE_UNKNOWN,
    SOURCE_CBZ,       // ZIP archive
    SOURCE_CBR,       // RAR archive
    SOURCE_DIRECTORY, // Directory of images
    SOURCE_PDF        // PDF document
} SourceType;

// Initialize the comic viewer subsystems
// monitor_index: Index of the monitor to use (-1 for default)
bool comic_viewer_init(int monitor_index);

// Load a comic file or directory
bool comic_viewer_load(const char *path);

// Run the main viewer loop
void comic_viewer_run(void);

// Clean up resources
void comic_viewer_cleanup(void);

#endif // COMIC_VIEWER_H
