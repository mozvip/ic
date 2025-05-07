/**
 * comic_viewer.h
 * Header file for the comic viewer implementation
 */

#ifndef COMIC_VIEWER_H
#define COMIC_VIEWER_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

// Maximum number of images we can handle
#define MAX_IMAGES 1000

// Image entry structure used by loaders
typedef struct {
    char *path;               // Path to the image
    SDL_Surface *surface;     // Loaded surface
    SDL_Texture *texture;     // Loaded texture
    float width;                // Original image width
    float height;               // Original image height
    SDL_FRect crop_rect;       // Crop rectangle for the image
} ImageEntry;


// Source type enumeration
typedef enum {
    SOURCE_UNKNOWN,
    SOURCE_CBZ,       // ZIP archive
    SOURCE_CBR,       // RAR archive
    SOURCE_DIRECTORY, // Directory of images
    SOURCE_PDF        // PDF document
} SourceType;

// Archive handle for on-demand loading
typedef enum {
    ARCHIVE_TYPE_NONE,
    ARCHIVE_TYPE_CBZ,
    ARCHIVE_TYPE_CBR,
    ARCHIVE_TYPE_PDF
} ArchiveType;

// Complete definition of ArchiveHandle structure
typedef struct ArchiveHandle {
    ArchiveType type;           // Type of archive
    char *path;                 // Path to the archive file
    int total_images;           // Total number of images in the archive
    char *temp_dir;             // Temporary directory for extracted files
    void *archive_ptr;          // Pointer to the archive-specific handle
    char **entry_names;         // Array of entry names (for CBZ/CBR)
    int *page_indices;          // Array of page indices (for PDF)
} ArchiveHandle;

#define MAX_IMAGES_PER_VIEW 10

typedef struct ImageView {
    int image_indices[MAX_IMAGES_PER_VIEW];  // Indices of images in this view
    int count;                              // Number of images in this view
} ImageView;

// Define the ViewerState struct
struct ViewerState {
    SourceType type;          // Type of source (CBZ, CBR, directory)
    char *source_path;        // Path to the source
    ImageEntry images[MAX_IMAGES];  // Array of image entries
    int image_count;          // Number of images
    int current_image;        // Current image index
    int preloaded_image;      // Index of preloaded image (next image)
    SDL_Window *window;       // Main SDL window
    SDL_Renderer *renderer;   // SDL renderer
    int window_width;         // Window width
    int window_height;        // Window height
    int drawable_width;      // Drawable width (for HiDPI)
    int drawable_height;     // Drawable height (for HiDPI)
    bool running;             // Main loop control
    bool fullscreen;          // Fullscreen state
    TTF_Font *font;           // Font for rendering text
    int monitor_index;        // Selected monitor index
    ArchiveHandle *archive;   // Handle for on-demand loading

    // Page-turn animation state
    bool page_turning_enabled;              // Whether a page-turn animations are enabled
    bool page_turning_in_progress;  // Whether a page-turn animation is in progress
    float page_turn_progress; // Progress of the page-turn animation (0.0 to 1.0)
    int target_image;         // The target image index for the page turn
    int direction;          // Direction of the page turn (1 for next, -1 for previous)

    // Progress indicator display timer
    Uint64 last_page_change_time;  // Time when the last page change occurred
    bool show_progress_indicator;  // Whether to show the progress indicator

    // Multi-image display settings
    bool multiple_images_mode;     // Whether to display multiple images
    int images_per_view;           // Number of images to display (typically 2)
    ImageView *views;              // Array of views
    int current_view;              // Current view index
    int view_count;                // Total number of views
    bool right_to_left;            // Reading direction (for manga)
    bool auto_detect_spreads;      // Automatically detect two-page spreads
};

// Declare viewer as an extern variable of this struct type
extern struct ViewerState viewer;

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
