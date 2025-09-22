/**
 * comic_viewer.h
 * Header file for the comic viewer implementation
 */

#ifndef COMIC_VIEWER_H
#define COMIC_VIEWER_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>
#include <FreeImage.h>
#include "edges.h"
#include "clipboard.h"

// Maximum number of images we can handle
#define MAX_IMAGES 1000

// Image entry structure used by loaders
typedef struct {
    char *path;               // Path to the image
    SDL_Surface *surface;     // Loaded surface
    FIBITMAP *bitmap;         // FreeImage bitmap (if used)
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
    SDL_Mutex *archive_mutex;   // Mutex to synchronize archive access across threads
} ArchiveHandle;

#define MAX_IMAGES_PER_VIEW 4

typedef struct ImageView {
    int image_indices[MAX_IMAGES_PER_VIEW];     // Indices of images in this view
    int count;                                  // Number of images in this view
    SDL_FRect crop_rect;                        // Crop rectangle for the view
    SDL_Surface *surface;                       // Combined surface for the view
    SDL_Texture *texture;                       // Loaded texture
    struct ImageView *next;                     // Pointer to next view in linked list
    struct ImageView *prev;                     // Pointer to previous view in linked list
    SDL_Color left_edge_color;                // Dominant color on the left edge
    SDL_Color right_edge_color;               // Dominant color on the right edge
} ImageView;

// Define the ViewerState struct
struct ViewerState {
    SourceType type;          // Type of source (CBZ, CBR, directory)
    char *source_path;        // Path to the source
    ImageEntry images[MAX_IMAGES];  // Array of image entries
    int image_count;          // Number of images
    SDL_Window *window;       // Main SDL window
    SDL_Renderer *renderer;   // SDL renderer
    int window_width;         // Window width
    int window_height;        // Window height
    int drawable_width;       // Drawable width (for HiDPI)
    int drawable_height;      // Drawable height (for HiDPI)
    bool running;             // Main loop control
    bool fullscreen;          // Fullscreen state
    TTF_Font *font;           // Font for rendering text
    int monitor_index;        // Selected monitor index
    ArchiveHandle *archive;   // Handle for on-demand loading
    SDL_Thread *load_thread;                   // Thread for loading images

    // Progress indicator display timer
    Uint64 last_page_change_time;  // Time when the last page change occurred
    bool show_progress_indicator;  // Whether to show the progress indicator

    // Multi-image display settings
    bool multiple_images_mode;     // Whether to display multiple images
    ImageView *first_view;         // Pointer to first view in linked list
    ImageView *current_view_node;  // Pointer to current view node
    int current_view_index;        // Current view index (for compatibility)
    int view_count;                // Total number of views

    // Zoom and pan settings
    float zoom_level;              // Current zoom level (1.0 = 100%)
    bool zoomed;                   // Whether we are currently in zoomed mode
    float zoom_center_x;           // X-coordinate center of zoom (in window coordinates)
    float zoom_center_y;           // Y-coordinate center of zoom (in window coordinates)
    float max_zoom;                // Maximum zoom level (e.g., 3.0 = 300%)
    float pan_offset_x;            // X offset for panning
    float pan_offset_y;            // Y offset for panning

    // Panning dynamics (velocity/acceleration to allow easing/acceleration)
    float pan_velocity_x;          // Current pan velocity X (pixels/sec)
    float pan_velocity_y;          // Current pan velocity Y (pixels/sec)
    float pan_acceleration;        // Acceleration applied when cursor at edge (pixels/sec^2)
    float pan_max_speed;           // Maximum panning speed (pixels/sec)
    float pan_damping;             // Damping factor applied when cursor leaves edges (per second)
    Uint64 last_update_ticks;      // Last tick used to compute dt for panning

    // User event for preloading
    Uint32 preload_event_type;
};

// Declare viewer as an extern variable of this struct type
extern struct ViewerState viewer;

// Initialize the comic viewer subsystems
// monitor_index: Index of the monitor to use (-1 for default)
bool comic_viewer_init(int monitor_index);

// Load a comic file or directory
bool comic_viewer_load(const char *path);
bool comic_viewer_load_and_display(const char *path_to_folder, const char *image_file_to_display);

// Run the main viewer loop
void comic_viewer_run(void);

// Clean up resources
void comic_viewer_cleanup(void);

// Provide limited public helpers needed by file browser
SDL_Texture* viewer_render_text(const char *text, SDL_Color color);
void viewer_init_view(ImageView *view);
bool viewer_has_current_view(void);

#endif // COMIC_VIEWER_H
