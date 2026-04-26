/**
 * comic_viewer.h
 * Header file for the comic viewer implementation
 */

#ifndef COMIC_VIEWER_H
#define COMIC_VIEWER_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>
#include "edges.h"
#include "clipboard.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of images we can handle
#define MAX_IMAGES 1000

// Image entry structure used by loaders
typedef struct {
    char *path;               // Path to the image
    SDL_Surface *surface;     // Loaded surface
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

// PDF rendering backend selection
typedef enum {
    PDF_BACKEND_POPPLER = 0,   // poppler-utils CLI (pdftoppm/pdfinfo)
    PDF_BACKEND_MUPDF   = 1,   // MuPDF C library
    PDF_BACKEND_COUNT
} PdfBackendType;

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
// Overlay mode enumeration
typedef enum {
    OVERLAY_GRADIENT,
    OVERLAY_STRETCHED,
    OVERLAY_AMBILIGHT
} OverlayMode;

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
    SDL_AtomicInt load_generation;             // Generation token for active load work

    // Progress indicator display timer
    Uint64 last_page_change_time;  // Time when the last page change occurred
    bool show_progress_indicator;  // Whether to show the progress indicator
    bool show_zoom_pan_info;       // Whether to show zoom/pan info overlay (toggled by 'i')

    // Visual settings
    OverlayMode overlay_mode;      // Rendering mode for empty space
    SDL_ScaleMode scale_mode;      // Texture scaling mode (Linear/Nearest)
    bool border_removal_enabled;   // Auto-trim white page borders
    int border_white_threshold;    // RGB average threshold for white border detection (0-255)
    int border_required_non_white; // Non-white samples needed to stop border scan

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

    // Cropping interaction state
    bool cropping_mode;            // Whether we are in cropping mode
    bool cropping_active;          // Whether a crop drag is active
    float crop_start_x;            // Crop drag start X (window coords)
    float crop_start_y;            // Crop drag start Y (window coords)
    float crop_current_x;          // Current cursor X during crop (window coords)
    float crop_current_y;          // Current cursor Y during crop (window coords)
    Uint8 cropping_button;         // Mouse button used to start the crop

    // User event for preloading
    Uint32 preload_event_type;

    // Gamepad state
    SDL_Gamepad *gamepad;          // Opened gamepad handle (if any)
    SDL_JoystickID gamepad_id;     // ID of the opened gamepad
    char gamepad_status_msg[128];  // Short status message to display (e.g. "Gamepad connected")
    Uint64 gamepad_status_until;   // Timestamp in ms until which the status should be shown

    // Status/error message displayed via ImGui (e.g. "No images found")
    char status_message[256];

    // PDF rendering backend
    PdfBackendType pdf_backend;  // Currently selected PDF backend
};

// Declare viewer as an extern variable of this struct type
extern struct ViewerState viewer;

// Initialize the comic viewer subsystems
// monitor_index: Index of the monitor to use (-1 for default)
bool comic_viewer_init(int monitor_index);

// Set preferred image backend: auto|freeimage|sdl_image.
bool comic_viewer_set_image_backend(const char *backend_name);

// Reload currently visible view so runtime decoder/processing changes apply immediately.
void comic_viewer_reload_current_view(void);

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

#ifdef __cplusplus
}
#endif

#endif // COMIC_VIEWER_H
