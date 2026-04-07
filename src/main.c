/**
 * ic - Image Comic viewer
 * A simple CBZ/CBR viewer that can also display folders of images
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "comic_viewer.h"
#include "comic_loaders.h"
#include "file_browser.h"
#include "imgui_layer.h"

void print_usage(const char *program_name) {
    printf("Usage: %s [options] <file_or_directory>\n", program_name);
    printf("Options:\n");
    printf("  -h, --help     Display this help message\n");
    printf("  -m, --monitor <index>  Specify which monitor to use (0 is primary)\n");
    printf("  --overlay <mode>       Specify overlay mode: gradient (default), stretched, ambilight\n");
    printf("  --image-backend <name> Select image backend: auto, freeimage, sdl_image\n");
    printf("\n");
    printf("Supported formats:\n");
    printf("  - CBZ files (Comic ZIP archives)\n");
    printf("  - CBR files (Comic RAR archives)\n");
    printf("  - PDF files\n");
    printf("  - Directories containing images\n");
}

int main(int argc, char *argv[]) {
    // Check if help was requested
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    int monitor_index = 0;  // Default to primary monitor
    const char *path = NULL;
    const char *image_backend = NULL;
    int i;
    
    // Parse command line options
    for (i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--monitor") == 0) && i + 1 < argc) {
            monitor_index = atoi(argv[i + 1]);
            i++;  // Skip the next argument (the monitor index)
        } else if (strcmp(argv[i], "--overlay") == 0 && i + 1 < argc) {
             const char *mode = argv[i + 1];
             if (strcmp(mode, "stretched") == 0) {
                 viewer.overlay_mode = OVERLAY_STRETCHED;
             } else if (strcmp(mode, "gradient") == 0) {
                 viewer.overlay_mode = OVERLAY_GRADIENT;
             } else if (strcmp(mode, "ambilight") == 0) {
                 viewer.overlay_mode = OVERLAY_AMBILIGHT;
             } else {
                 fprintf(stderr, "Unknown overlay mode: %s. Using default (gradient).\n", mode);
             }
             i++;
        } else if (strcmp(argv[i], "--image-backend") == 0 && i + 1 < argc) {
            image_backend = argv[i + 1];
            i++;
        } else if (path == NULL && argv[i][0] != '-') {
            path = argv[i];
        }
    }

    if (image_backend && !comic_viewer_set_image_backend(image_backend)) {
        fprintf(stderr,
                "Invalid or unavailable image backend '%s' (expected auto, freeimage, or sdl_image)\n",
                image_backend);
        return 1;
    }
    
    // Initialize the comic viewer
    if (!comic_viewer_init(monitor_index)) {
        fprintf(stderr, "Failed to initialize comic viewer\n");
        return 1;
    }

    // Parse command line options again for settings that need initialization
    // The previous loop was just for pre-init settings like monitor
    // But we need to set overlay_mode after init because init sets the default
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--overlay") == 0 && i + 1 < argc) {
             const char *mode = argv[i + 1];
             if (strcmp(mode, "stretched") == 0) {
                 viewer.overlay_mode = OVERLAY_STRETCHED;
             } else if (strcmp(mode, "gradient") == 0) {
                 viewer.overlay_mode = OVERLAY_GRADIENT;
             }
             i++;
        }
    }

    int return_value = 0;
    // Load the comic or directory
    if (path) {
        if (comic_viewer_load(path)) {
            // Run the main loop
            comic_viewer_run();
        } else {
            // Loading failed — show the status message and open the file browser
            if (viewer.status_message[0] != '\0') {
                imgui_layer_set_status_message(viewer.status_message);
            }
            file_browser_open(path);
            comic_viewer_run();
        }
    } else {
        // No path provided, start in file browser mode
        file_browser_open(NULL);
        comic_viewer_run();
    }


    // Clean up
    comic_viewer_cleanup();

    return return_value;
}
