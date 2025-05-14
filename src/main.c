/**
 * ic - Image Comic viewer
 * A simple CBZ/CBR viewer that can also display folders of images
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "comic_viewer.h"
#include "comic_loaders.h"

void print_usage(const char *program_name) {
    printf("Usage: %s [options] <file_or_directory>\n", program_name);
    printf("Options:\n");
    printf("  -h, --help     Display this help message\n");
    printf("  -m, --monitor <index>  Specify which monitor to use (0 is primary)\n");
    printf("\n");
    printf("Supported formats:\n");
    printf("  - CBZ files (Comic ZIP archives)\n");
    printf("  - CBR files (Comic RAR archives)\n");
    printf("  - Directories containing images\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // Check if help was requested
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    int monitor_index = 0;  // Default to primary monitor
    int i;
    
    // Parse command line options
    for (i = 1; i < argc - 1; i++) {
        if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--monitor") == 0) && i + 1 < argc) {
            monitor_index = atoi(argv[i + 1]);
            i++;  // Skip the next argument (the monitor index)
        }
    }

    const char *path = argv[argc - 1];
    
    // Initialize the comic viewer
    if (!comic_viewer_init(monitor_index)) {
        fprintf(stderr, "Failed to initialize comic viewer\n");
        return 1;
    }

    int return_value = 0;
    // Load the comic or directory
    if (comic_viewer_load(path)) {
        // Run the main loop
        comic_viewer_run();
    } else {
        fprintf(stderr, "Failed to load: %s\n", path);
        return_value = 1;
    }

    // Clean up
    comic_viewer_cleanup();

    return return_value;
}
