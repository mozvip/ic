/**
 * ic - Image Comic viewer
 * A simple CBZ/CBR viewer that can also display folders of images
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include "comic_viewer.h"
#include "comic_loaders.h"
#include "file_browser.h"
#include "imgui_layer.h"
#include "linux_desktop.h"

void print_usage(const char *program_name) {
    printf("Usage: %s [options] [--] [file_or_directory]\n", program_name);
    printf("Options:\n");
    printf("  -h, --help     Display this help message\n");
    printf("  --version     Display the version\n");
    printf("  --accessible  Open native screen-reader-accessible reading controls\n");
    printf("  -m, --monitor <index>  Display index, or -1 for the primary display (default)\n");
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
    linux_desktop_init();
    bool accessible_controls = false;
    int monitor_index = -1;
    const char *path = NULL;
    const char *image_backend = NULL;
    OverlayMode overlay = OVERLAY_GRADIENT;
    static const struct option arguments[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {"accessible", no_argument, NULL, 'a'},
        {"monitor", required_argument, NULL, 'm'},
        {"overlay", required_argument, NULL, 'o'},
        {"image-backend", required_argument, NULL, 'b'},
        {NULL, 0, NULL, 0}
    };
    int argument;
    while ((argument = getopt_long(argc, argv, "hvm:", arguments, NULL)) != -1) {
        switch (argument) {
            case 'h': print_usage(argv[0]); return 0;
            case 'v': printf("ic %s\n", IC_VERSION); return 0;
            case 'a': accessible_controls = true; break;
            case 'm': {
                char *end;
                errno = 0;
                long index = strtol(optarg, &end, 10);
                if (errno || !*optarg || *end || index < -1 || index > INT_MAX) {
                    fprintf(stderr, "Invalid monitor index: %s\n", optarg);
                    return 2;
                }
                monitor_index = (int)index;
                break;
            }
            case 'b': image_backend = optarg; break;
            case 'o':
                if (!strcmp(optarg, "gradient")) overlay = OVERLAY_GRADIENT;
                else if (!strcmp(optarg, "stretched")) overlay = OVERLAY_STRETCHED;
                else if (!strcmp(optarg, "ambilight")) overlay = OVERLAY_AMBILIGHT;
                else { fprintf(stderr, "Invalid overlay: %s\n", optarg); return 2; }
                break;
            default: return 2;
        }
    }
    if (argc - optind > 1) { fprintf(stderr, "Only one source can be opened at a time\n"); return 2; }
    if (optind < argc) path = argv[optind];

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

    viewer.overlay_mode = overlay;
    if (accessible_controls) linux_desktop_show_controls();

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
