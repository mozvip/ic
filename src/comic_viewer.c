/**
 * comic_viewer.c
 * Implementation of the comic viewer functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL_hints.h>

#include "comic_viewer.h"
#include "temp_utils.h"
#include "linux_desktop.h"
#include <glib.h>
#include "comic_loaders.h"
#include "pdf_backend.h"
#include "progress_bar.h"
#include "progress_indicator.h" // Moved here
#include "image_loader.h"
#include "upscale.h"
// Rendering module (viewer_display_info, viewer_render_current_view, viewer_render_text)
#include "render.h"
#include "imgui_layer.h"
#include "gpu_shader.h"

SDL_Color white = {255, 255, 255, 255}; // White

struct ViewerState viewer; // Define the global viewer variable

// Global state for image enhancement options
ImageProcessingOptions* options;

// Forward declarations for internal functions
static void free_resources(void);
static void handle_events(void);
static void update_state(void);
static bool prepare_image(int index);
static void unload_image(ImageEntry *image);
static void toggle_fullscreen(void);
static void view_changed(ImageView *old_view_node, ImageView *new_view_node);
static bool select_monitor(int monitor_index, int *x, int *y);
// Preload thread function and starter
static int load_view_surfaces_in_thread(void *data);
static void load_view(ImageView *view);
void unload_view(ImageView *view);
// Structure sent from worker thread to main thread when preload surfaces are ready
typedef struct PreloadResult {
    ImageView *view;
    int generation;
} PreloadResult;

typedef struct LoadViewTask {
    ImageView *view;
    int generation;
} LoadViewTask;
static void create_texture(SDL_Renderer *renderer, ImageView *view);
static void update_progress(float progress, const char *message);
static void generate_default_views(void);
static void go_to_previous_view(void);
static void go_to_next_view(void);
void free_view_texture(ImageView *view) ;
static void viewer_state_restore_for_source(void);

static SDL_Renderer *create_renderer_prefer_gpu_vulkan(SDL_Window *window);
// Provided by comic_loaders_utils.c
extern const char* get_filename_from_path(const char* path);
// File browser (separate module)
#include "file_browser.h"

typedef struct ViewerPersistedState {
    int view_index;
    int zoomed;
    float zoom_level;
    float zoom_center_x;
    float zoom_center_y;
    float pan_offset_x;
    float pan_offset_y;
} ViewerPersistedState;

static char *viewer_state_build_key(const char *source_path, char *out, size_t out_size) {
    if (!source_path || !out || out_size == 0) {
        return NULL;
    }

    char resolved[PATH_MAX];
    if (realpath(source_path, resolved)) {
        strncpy(out, resolved, out_size - 1);
    } else {
        strncpy(out, source_path, out_size - 1);
    }
    out[out_size - 1] = '\0';
    return out;
}

static char *viewer_state_file_path(void) {
    char root[PATH_MAX];
    if (!temp_utils_get_state_root(root, sizeof(root))) return NULL;
    char *full = malloc(strlen(root) + sizeof("/viewer_state.tsv"));
    if (!full) return NULL;
    sprintf(full, "%s/viewer_state.tsv", root);
    if (access(full, F_OK) != 0) {
        char *old_root = SDL_GetPrefPath("mozvip", "ic");
        if (old_root) {
            char *old_path = g_build_filename(old_root, "viewer_state.tsv", NULL);
            gchar *contents = NULL;
            gsize size = 0;
            if (g_file_get_contents(old_path, &contents, &size, NULL)) {
                g_file_set_contents_full(full, contents, size, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
            }
            g_free(contents);
            g_free(old_path);
            SDL_free(old_root);
        }
    }
    return full;
}

static bool viewer_state_parse_line(const char *line, char *path, size_t path_size, ViewerPersistedState *state) {
    if (!line || !path || !state || path_size == 0) {
        return false;
    }

    char buf[8192];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *newline = strchr(buf, '\n');
    if (newline) {
        *newline = '\0';
    }

    char *saveptr = NULL;
    char *token = strtok_r(buf, "\t", &saveptr);
    if (!token) {
        return false;
    }
    strncpy(path, token, path_size - 1);
    path[path_size - 1] = '\0';

    token = strtok_r(NULL, "\t", &saveptr);
    if (!token) return false;
    state->view_index = atoi(token);

    token = strtok_r(NULL, "\t", &saveptr);
    if (!token) return false;
    state->zoomed = atoi(token);

    token = strtok_r(NULL, "\t", &saveptr);
    if (!token) return false;
    state->zoom_level = strtof(token, NULL);

    token = strtok_r(NULL, "\t", &saveptr);
    if (!token) return false;
    state->zoom_center_x = strtof(token, NULL);

    token = strtok_r(NULL, "\t", &saveptr);
    if (!token) return false;
    state->zoom_center_y = strtof(token, NULL);

    token = strtok_r(NULL, "\t", &saveptr);
    if (!token) return false;
    state->pan_offset_x = strtof(token, NULL);

    token = strtok_r(NULL, "\t", &saveptr);
    if (!token) return false;
    state->pan_offset_y = strtof(token, NULL);

    return true;
}

static void viewer_state_restore_for_source(void) {
    if (!viewer.source_path || viewer.view_count <= 0 || !viewer.first_view) {
        return;
    }

    char key[PATH_MAX];
    if (!viewer_state_build_key(viewer.source_path, key, sizeof(key))) {
        return;
    }

    char *state_path = viewer_state_file_path();
    if (!state_path) {
        return;
    }

    FILE *f = fopen(state_path, "r");
    free(state_path);
    if (!f) {
        return;
    }

    char line[8192];
    bool found = false;
    ViewerPersistedState st = {0};
    while (fgets(line, sizeof(line), f)) {
        char line_path[PATH_MAX];
        ViewerPersistedState tmp;
        if (!viewer_state_parse_line(line, line_path, sizeof(line_path), &tmp)) {
            continue;
        }
        if (strcmp(line_path, key) == 0) {
            st = tmp;
            found = true;
            break;
        }
    }
    fclose(f);

    if (!found) {
        return;
    }

    if (st.view_index < 0) {
        st.view_index = 0;
    }
    if (st.view_index >= viewer.view_count) {
        st.view_index = viewer.view_count - 1;
    }

    ImageView *node = viewer.first_view;
    int idx = 0;
    while (node && idx < st.view_index) {
        node = node->next;
        idx++;
    }
    if (node) {
        viewer.current_view_node = node;
        viewer.current_view_index = st.view_index;
    }

    viewer.zoomed = (st.zoomed != 0);
    viewer.zoom_level = st.zoom_level;
    if (viewer.zoom_level < 1.0f) {
        viewer.zoom_level = 1.0f;
    }
    if (viewer.zoom_level > viewer.max_zoom) {
        viewer.zoom_level = viewer.max_zoom;
    }
    viewer.zoom_center_x = st.zoom_center_x;
    viewer.zoom_center_y = st.zoom_center_y;
    viewer.pan_offset_x = st.pan_offset_x;
    viewer.pan_offset_y = st.pan_offset_y;

    if (!viewer.zoomed) {
        viewer.zoom_level = 1.0f;
        viewer.pan_offset_x = 0.0f;
        viewer.pan_offset_y = 0.0f;
    }
}

void comic_viewer_save_reading_position(void) {
    if (!viewer.source_path || viewer.view_count <= 0 || !viewer.current_view_node) {
        return;
    }

    char key[PATH_MAX];
    if (!viewer_state_build_key(viewer.source_path, key, sizeof(key))) {
        return;
    }

    char *state_path = viewer_state_file_path();
    if (!state_path) {
        return;
    }

    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", state_path);

    FILE *in = fopen(state_path, "r");
    FILE *out = fopen(tmp_path, "w");
    if (!out) {
        if (in) fclose(in);
        free(state_path);
        return;
    }

    bool replaced = false;
    char line[8192];
    if (in) {
        while (fgets(line, sizeof(line), in)) {
            char line_path[PATH_MAX];
            ViewerPersistedState parsed;
            if (viewer_state_parse_line(line, line_path, sizeof(line_path), &parsed) && strcmp(line_path, key) == 0) {
                fprintf(out, "%s\t%d\t%d\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
                        key,
                        viewer.current_view_index,
                        viewer.zoomed ? 1 : 0,
                        viewer.zoom_level,
                        viewer.zoom_center_x,
                        viewer.zoom_center_y,
                        viewer.pan_offset_x,
                        viewer.pan_offset_y);
                replaced = true;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }

    if (!replaced) {
        fprintf(out, "%s\t%d\t%d\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
                key,
                viewer.current_view_index,
                viewer.zoomed ? 1 : 0,
                viewer.zoom_level,
                viewer.zoom_center_x,
                viewer.zoom_center_y,
                viewer.pan_offset_x,
                viewer.pan_offset_y);
    }

    fclose(out);
    rename(tmp_path, state_path);
    free(state_path);
}

bool comic_viewer_set_image_backend(const char *backend_name) {
    ImageBackend backend = image_loader_parse_backend(backend_name);
    if (backend == IMAGE_BACKEND_COUNT) {
        return false;
    }
    comic_viewer_finish_loading();
    return image_loader_set_preferred_backend(backend);
}

void comic_viewer_reload_current_view(void) {
    if (!viewer.current_view_node) {
        return;
    }

    unload_view(viewer.current_view_node);
    load_view(viewer.current_view_node);
}


// ---------------- Remaining Viewer Implementation ----------------
// Linked list helper functions
static ImageView* create_view_node_after(ImageView *prev_view);
static void append_view(ImageView *view);
static void free_all_views(void);

static int get_current_view(void) {
    return viewer.current_view_index;
}

static void remove_current_view(void) {
    if (!viewer.current_view_node || viewer.view_count <= 1) {
        // Can't remove if no current view or only one view left
        return;
    }
    
    comic_viewer_finish_loading();
    ImageView *view_to_remove = viewer.current_view_node;
    ImageView *next_view = view_to_remove->next;
    ImageView *prev_view = view_to_remove->prev;

    unload_view(view_to_remove);
    
    // Update linked list connections
    if (prev_view) {
        prev_view->next = next_view;
    } else {
        // Removing the first view
        viewer.first_view = next_view;
    }
    
    if (next_view) {
        next_view->prev = prev_view;
    }
    
    // Move to the next view, or previous if no next
    if (next_view) {
        viewer.current_view_node = next_view;
        // current_view_index stays the same since we removed the current view
    } else if (prev_view) {
        viewer.current_view_node = prev_view;
        viewer.current_view_index--;
    }
    
    // Free the removed view
    free(view_to_remove);
    viewer.view_count--;
    
    // Load images for the new current view
    if (viewer.current_view_node) {
        for (int i = 0; i < viewer.current_view_node->count; i++) {
            int image_index = viewer.current_view_node->image_indices[i];
            if (image_index >= 0 && image_index < viewer.image_count) {
                prepare_image(image_index);
            }
        }
        
        // Update page change time for progress indicator
        viewer.last_page_change_time = SDL_GetTicks();
    }
}

// ---------------- Delete from disk (directory mode) ----------------

// Window during which a second Delete press confirms the erase.
#define DELETE_CONFIRM_MS 3000

// Path armed by the first Delete press; the second press only commits if the
// current view still shows this exact file.
static char delete_confirm_path[PATH_MAX];
static bool delete_confirm_permanent;
static Uint64 delete_confirm_until = 0;

static void set_transient_status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    SDL_vsnprintf(viewer.gamepad_status_msg, sizeof(viewer.gamepad_status_msg), fmt, args);
    va_end(args);
    viewer.gamepad_status_until = SDL_GetTicks() + 3000;
}

void comic_viewer_finish_loading(void) {
    if (viewer.load_thread) {
        SDL_WaitThread(viewer.load_thread, NULL);
        viewer.load_thread = NULL;
    }
    SDL_Event events[16];
    if (viewer.preload_event_type >= SDL_EVENT_USER) {
        int count;
        while ((count = SDL_PeepEvents(events, 16, SDL_GETEVENT,
                                       viewer.preload_event_type, viewer.preload_event_type)) > 0) {
            for (int i = 0; i < count; i++) {
                PreloadResult *res = events[i].user.data1;
                if (res && res->generation == SDL_GetAtomicInt(&viewer.load_generation) &&
                    res->view && viewer.renderer) {
                    create_texture(viewer.renderer, res->view);
                }
                free(res);
            }
        }
    }
    SDL_AddAtomicInt(&viewer.load_generation, 1);
}

// Drop images[index] from the flat page array and shift the tail down, fixing up
// every view index that pointed past it. Callers must already have detached any
// view referencing the entry.
static void remove_image_entry(int index) {
    if (index < 0 || index >= viewer.image_count) return;

    free(viewer.images[index].path);
    if (viewer.images[index].surface) {
        SDL_DestroySurface(viewer.images[index].surface);
    }

    for (int i = index; i < viewer.image_count - 1; i++) {
        viewer.images[i] = viewer.images[i + 1];
    }
    viewer.image_count--;
    memset(&viewer.images[viewer.image_count], 0, sizeof(ImageEntry));

    for (ImageView *view = viewer.first_view; view; view = view->next) {
        for (int i = 0; i < view->count; i++) {
            if (view->image_indices[i] > index) {
                view->image_indices[i]--;
            }
        }
    }
}

static void delete_current_view_from_disk(bool permanent) {
    ImageView *view = viewer.current_view_node;
    if (!view || view->count <= 0) return;

    const char *first_path = viewer.images[view->image_indices[0]].path;
    if (!first_path) return;

    if (viewer.view_count <= 1) {
        set_transient_status("Cannot delete the last remaining image");
        return;
    }

    Uint64 now = SDL_GetTicks();
    bool armed = now <= delete_confirm_until &&
                 permanent == delete_confirm_permanent &&
                 strcmp(delete_confirm_path, first_path) == 0;
    if (!armed) {
        SDL_strlcpy(delete_confirm_path, first_path, sizeof(delete_confirm_path));
        delete_confirm_until = now + DELETE_CONFIRM_MS;
        delete_confirm_permanent = permanent;
        if (view->count > 1) {
            set_transient_status("Press %s again to %s %d files", permanent ? "Shift+Delete" : "Delete",
                                 permanent ? "permanently erase" : "trash", view->count);
        } else {
            set_transient_status("Press %s again to %s %s", permanent ? "Shift+Delete" : "Delete",
                                 permanent ? "permanently erase" : "trash",
                                 get_filename_from_path(first_path));
        }
        return;
    }
    delete_confirm_until = 0;
    delete_confirm_path[0] = '\0';

    // A worker may still be decoding these images; join it before unlinking so it
    // never reads viewer.images[] entries we are about to tear down.
    comic_viewer_finish_loading();

    // Snapshot the indices in descending order so compacting the array below
    // never invalidates an index we have not handled yet.
    int indices[MAX_IMAGES_PER_VIEW];
    int index_count = view->count;
    for (int i = 0; i < index_count; i++) {
        indices[i] = view->image_indices[i];
    }
    for (int i = 0; i < index_count - 1; i++) {
        for (int j = i + 1; j < index_count; j++) {
            if (indices[j] > indices[i]) {
                int tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }

    bool unlinked[MAX_IMAGES_PER_VIEW] = { false };
    int deleted_count = 0;
    char last_error[256] = "";
    char last_name[256] = "";
    for (int i = 0; i < index_count; i++) {
        const char *path = viewer.images[indices[i]].path;
        if (!path) continue;
        bool removed = permanent ? remove(path) == 0 : linux_desktop_trash_file(path, last_error, sizeof(last_error));
        if (!removed) {
            if (permanent) {
                snprintf(last_error, sizeof(last_error), "%s: %s",
                         get_filename_from_path(path), strerror(errno));
            }
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to delete %s: %s", path, last_error);
            continue;
        }
        unlinked[i] = true;
        deleted_count++;
        snprintf(last_name, sizeof(last_name), "%s", get_filename_from_path(path));
    }

    if (deleted_count == 0) {
        set_transient_status("Delete failed - %s", last_error);
        return;
    }

    // Detach the view first, then compact the entries whose file is really gone.
    remove_current_view();
    for (int i = 0; i < index_count; i++) {
        if (unlinked[i]) {
            remove_image_entry(indices[i]);
        }
    }

    if (viewer.current_view_node && !viewer.current_view_node->texture) {
        load_view(viewer.current_view_node);
    }

    if (last_error[0] != '\0') {
        set_transient_status("%s %d file(s), failed on %s", permanent ? "Deleted" : "Trashed", deleted_count, last_error);
    } else if (deleted_count > 1) {
        set_transient_status("%s %d files", permanent ? "Deleted" : "Trashed", deleted_count);
    } else {
        set_transient_status("%s %s", permanent ? "Deleted" : "Trashed", last_name);
    }
}

bool comic_viewer_init(int monitor_index) {
    SDL_SetAppMetadata("IC", IC_VERSION, IC_APPLICATION_ID);
    SDL_SetHint(SDL_HINT_APP_ID, IC_APPLICATION_ID);
    // Set best quality for scaling operations
    SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "2");  // 0=nearest, 1=linear, 2="best"
    
    // Initialize SDL (include gamepad support if available)
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL could not initialize! SDL_Error: %s", SDL_GetError());
        return false;
    }
   
    // Initialize SDL_ttf
    if (!TTF_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_ttf could not initialize! SDL_ttf Error: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // Initialize image loader backend
    if (!image_loader_init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize image loader backend");
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    // Determine monitor position
    int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    if (!select_monitor(monitor_index, &x, &y)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to select monitor %d", monitor_index);
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    // Create window
    viewer.window_width = 1024;
    viewer.window_height = 768;
    viewer.monitor_index = monitor_index;
    
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    viewer.window = SDL_CreateWindow("IC - Image Comic Viewer", 
                                    viewer.window_width, 
                                    viewer.window_height,
                                    window_flags);
                                    
    if (viewer.window == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window could not be created! SDL_Error: %s", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    // Position the window on the selected monitor
    SDL_SetWindowPosition(viewer.window, x, y);

    // Create renderer (prefer GPU + Vulkan backends)
    viewer.renderer = create_renderer_prefer_gpu_vulkan(viewer.window);
    
    if (viewer.renderer == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Renderer could not be created! SDL_Error: %s", SDL_GetError());
        SDL_DestroyWindow(viewer.window);
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    if (!imgui_layer_init(viewer.window, viewer.renderer)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Warning: Failed to initialize ImGui overlay");
    }

    if (!gpu_shader_init(viewer.renderer)) {
        SDL_Log("GPU pixel shaders unavailable, using CPU fallback");
    }
    
    // Initialize the progress bar
    if (!progress_bar_init(viewer.renderer)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Warning: Failed to initialize progress bar. Loading will proceed without visual feedback.");
        // Continue without progress bar - non-fatal
    }
    
    // Get the actual window size and drawable size for HiDPI scaling
    SDL_GetWindowSizeInPixels(viewer.window, &viewer.drawable_width, &viewer.drawable_height);
    SDL_GetWindowSize(viewer.window, &viewer.window_width, &viewer.window_height);

    // Default scale mode
    viewer.scale_mode = SDL_SCALEMODE_LINEAR;
    
    // Set logical size to handle HiDPI scaling automatically
    SDL_SetRenderLogicalPresentation(viewer.renderer, viewer.window_width, viewer.window_height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (viewer.drawable_width > viewer.window_width || viewer.drawable_height > viewer.window_height) {
        printf("HiDPI detected: Window size: %dx%d, Drawable size: %dx%d\n", 
               viewer.window_width, viewer.window_height, 
               viewer.drawable_width, viewer.drawable_height);
    }

    viewer.font = linux_desktop_open_font(20);
    if (!viewer.font) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Warning: Failed to load font: %s", SDL_GetError());
        // Continue without a font - we'll handle the null case when rendering
    }

    // Initialize viewer state
    viewer.type = SOURCE_UNKNOWN;
    viewer.source_path = NULL;
    viewer.image_count = 0;
    viewer.current_view_index = 0;
    viewer.first_view = NULL;
    viewer.current_view_node = NULL;
    viewer.running = false;
    viewer.fullscreen = false;
    viewer.archive = NULL;
    viewer.load_thread = NULL;
    SDL_SetAtomicInt(&viewer.load_generation, 0);
    
    // Initialize progress indicator timer
    viewer.last_page_change_time = 0;
    viewer.show_progress_indicator = false;
    viewer.show_zoom_pan_info = true;

    // Initialize visual settings
    viewer.overlay_mode = OVERLAY_STRETCHED;
    viewer.border_removal_enabled = true;
    viewer.border_white_threshold = 240;
    viewer.border_required_non_white = 3;
    viewer.overlay_pixel_shader = PIXEL_SHADER_SCANLINES;
    viewer.view_pixel_shader = PIXEL_SHADER_NONE;

    // Initialize PDF backend (default to MuPDF)
    viewer.pdf_backend = PDF_BACKEND_MUPDF;
    viewer.pdf_dpi = PDF_DEFAULT_DPI;

    // Initialize zoom settings
    viewer.zoom_level = 1.0f;
    viewer.zoomed = false;
    viewer.zoom_center_x = 0;
    viewer.zoom_center_y = 0;
    viewer.max_zoom = 3.0f;
    viewer.pan_offset_x = 0;
    viewer.pan_offset_y = 0;
    // Initialize panning dynamics
    viewer.pan_velocity_x = 0.0f;
    viewer.pan_velocity_y = 0.0f;
    viewer.pan_acceleration = 200.0f; // pixels per second squared (starting acceleration)
    viewer.pan_max_speed = 1000.0f;   // max pixels per second
    viewer.pan_damping = 4.0f;        // damping factor per second when cursor leaves edge
    viewer.last_update_ticks = SDL_GetTicks();

    // Initialize cropping state
    viewer.cropping_mode = false;
    viewer.cropping_active = false;
    viewer.crop_start_x = viewer.crop_start_y = 0.0f;
    viewer.crop_current_x = viewer.crop_current_y = 0.0f;
    viewer.cropping_button = 0;

    // Initialize gamepad state
    viewer.gamepad = NULL;
    viewer.gamepad_id = 0;
    viewer.gamepad_status_msg[0] = '\0';
    viewer.gamepad_status_until = 0;

    for (int i = 0; i < MAX_IMAGES; i++) {
        viewer.images[i].path = NULL;
        viewer.images[i].surface = NULL;
    }

    // Register a user event type for preload completion
    viewer.preload_event_type = SDL_RegisterEvents(1);

    // Open the first available gamepad (if any)
    int gp_count = 0;
    SDL_JoystickID *gplist = SDL_GetGamepads(&gp_count);
    if (gplist && gp_count > 0) {
        viewer.gamepad = SDL_OpenGamepad(gplist[0]);
        if (viewer.gamepad) {
            viewer.gamepad_id = SDL_GetGamepadID(viewer.gamepad);
            const char *name = SDL_GetGamepadName(viewer.gamepad);
            snprintf(viewer.gamepad_status_msg, sizeof(viewer.gamepad_status_msg), "Gamepad connected: %s", name ? name : "(unknown)");
            viewer.gamepad_status_until = SDL_GetTicks() + 3000; // show for 3s
        }
    }
    if (gplist) SDL_free(gplist);

    return true;
}

static SDL_Renderer *create_renderer_prefer_gpu_vulkan(SDL_Window *window) {
    if (!window) return NULL;

    const char *hint_driver = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    if (hint_driver && hint_driver[0] != '\0') {
        SDL_Log("SDL_RENDER_DRIVER is set to '%s' (honoring override)", hint_driver);
        SDL_Renderer *r = SDL_CreateRenderer(window, hint_driver);
        if (r) {
            SDL_Log("Using renderer driver: %s", SDL_GetRendererName(r));
            return r;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create renderer using SDL_RENDER_DRIVER='%s': %s",
                    hint_driver, SDL_GetError());
        // fall through to preferred order
    }

    const char *preferred[] = {"gpu", "vulkan"};
    const int preferred_count = (int)(sizeof(preferred) / sizeof(preferred[0]));

    int num_renderers = SDL_GetNumRenderDrivers();
    SDL_Log("Available SDL render drivers (%d):", num_renderers);
    for (int i = 0; i < num_renderers; i++) {
        const char *driver = SDL_GetRenderDriver(i);
        SDL_Log("  %d: %s", i, driver ? driver : "(null)");
    }

    for (int p = 0; p < preferred_count; p++) {
        const char *want = preferred[p];
        for (int i = 0; i < num_renderers; i++) {
            const char *driver = SDL_GetRenderDriver(i);
            if (!driver) continue;
            if (SDL_strcasecmp(driver, want) == 0) {
                SDL_Log("Trying preferred renderer driver: %s", driver);
                SDL_Renderer *r = SDL_CreateRenderer(window, driver);
                if (r) {
                    SDL_Log("Using renderer driver: %s", SDL_GetRendererName(r));
                    return r;
                }
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Failed to create renderer '%s': %s",
                            driver, SDL_GetError());
                break;
            }
        }
    }

    SDL_Log("Falling back to SDL default renderer selection");
    SDL_Renderer *r = SDL_CreateRenderer(window, NULL);
    if (r) {
        SDL_Log("Using renderer driver: %s", SDL_GetRendererName(r));
    }
    return r;
}

bool comic_viewer_load_and_display(const char *path_to_folder, const char *image_file_to_display) {
    bool res = comic_viewer_load(path_to_folder);
    if (!res) return false;

    // Then load the specific image file is provided
    if (image_file_to_display == NULL) return true;

    // Find the index of the specified image file
    int target_index = -1;
    for (int i = 0; i < viewer.image_count; i++) {
        if (viewer.images[i].path && strcmp(viewer.images[i].path, image_file_to_display) == 0) {
            target_index = i;
            break;
        }
    }
    if (target_index == -1) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Specified image file not found in loaded images: %s", image_file_to_display);
        return false;
    }

    // Find which view contains this image index
    ImageView *view = viewer.first_view;
    int view_index = 0;
    while (view) {
        for (int i = 0; i < view->count; i++) {
            if (view->image_indices[i] == target_index) {
                view_changed(NULL, view);
                // Found the view containing the target image
                viewer.current_view_node = view;
                viewer.current_view_index = view_index;
                return true;
            }
        }
        view = view->next;
        view_index++;
    }

    return true;
}

// Split a single image file path into the folder that holds it and the image's
// own path, spelled exactly the way load_directory() builds its entries
// ("<folder>/<name>") so the two can be compared with strcmp.
static bool split_image_path(const char *path, char *folder, char *image, size_t size) {
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) return false;

    char *last_sep = strrchr(resolved, '/');
    if (!last_sep) return false;

    // Split in place: the name follows the last separator, the folder is what
    // precedes it ("/" when the file sits at the filesystem root).
    const char *name = last_sep + 1;
    *last_sep = '\0';
    const char *dir = (last_sep == resolved) ? "/" : resolved;

    int n = snprintf(image, size, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= size) return false;

    n = snprintf(folder, size, "%s", dir);
    if (n < 0 || (size_t)n >= size) return false;

    return true;
}

static bool initial_view_pending;
static int pending_preload_direction;

bool comic_viewer_load(const char *path) {
    comic_viewer_finish_loading();
    initial_view_pending = false;
    pending_preload_direction = 0;
    comic_viewer_save_reading_position();
    if (path == NULL) return false;

    // A lone image file: open the folder containing it and jump straight to it,
    // the same way the file browser opens an image.
    struct stat file_stat;
    if (stat(path, &file_stat) == 0 && S_ISREG(file_stat.st_mode) && is_image_file(path)) {
        char folder[PATH_MAX];
        char image[PATH_MAX];
        if (!split_image_path(path, folder, image, sizeof(folder))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot resolve image path: %s", path);
            return false;
        }
        return comic_viewer_load_and_display(folder, image);
    }

    // Store the source path
    viewer.source_path = strdup(path);
    if (viewer.source_path == NULL) return false;

    // Initial progress update
    progress_bar_update(0.0f, "Detecting input type...");

    // Check if path is a directory
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot access path: %s", path);
        return false;
    }

    bool result = false;

    // Determine source type
    if (S_ISDIR(path_stat.st_mode)) {
        viewer.type = SOURCE_DIRECTORY;
        result = load_directory(path, viewer.images, &viewer.image_count, MAX_IMAGES, update_progress);
    } else {
        // Check file extension to determine type
        size_t len = strlen(path);
        if (len > 4) {
            const char *ext = path + len - 4;
            if (strcasecmp(ext, ".cbz") == 0 || strcasecmp(ext, ".zip") == 0) {
                viewer.type = SOURCE_CBZ;
                viewer.archive = archive_open(path, ARCHIVE_TYPE_CBZ, &viewer.image_count, update_progress);
            } else if (strcasecmp(ext, ".cbr") == 0 || strcasecmp(ext, ".rar") == 0) {
                viewer.type = SOURCE_CBR;
                viewer.archive = archive_open(path, ARCHIVE_TYPE_CBR, &viewer.image_count, update_progress);
            } else if (strcasecmp(ext, ".pdf") == 0) {
                viewer.type = SOURCE_PDF;
                viewer.archive = archive_open(path, ARCHIVE_TYPE_PDF, &viewer.image_count, update_progress);
            }
            result = viewer.archive != NULL;
        }
    }

    if (result) {
        progress_bar_update(0.8f, "Building page views...");
        // Clear any previous status message
        viewer.status_message[0] = '\0';
        imgui_layer_set_status_message(NULL);
        // generate default views
        generate_default_views();
        progress_bar_update(0.9f, "Restoring reading position...");
        viewer_state_restore_for_source();
        progress_bar_update(0.95f, "Preparing viewer...");
        // Update window title to reflect loaded source filename (not full path)
        if (viewer.window && viewer.source_path) {
            const char *src = viewer.source_path;
            int len = (int)strlen(src);
            int end = len - 1;
            // Trim trailing slashes/backslashes
            while (end >= 0 && (src[end] == '/' || src[end] == '\\')) end--;

            const char *filename = src;
            if (end >= 0) {
                const char *last_sep = NULL;
                for (int i = 0; i <= end; ++i) {
                    if (src[i] == '/' || src[i] == '\\') last_sep = &src[i];
                }
                if (last_sep) filename = last_sep + 1;
            }

            char title[1024];
            snprintf(title, sizeof(title), "IC - %s", filename);
            SDL_SetWindowTitle(viewer.window, title);
        }
        initial_view_pending = true;
        progress_bar_update(0.96f, "Loading and processing first page...");
        progress_bar_render();
    } else {
        progress_bar_update(1.0f, "Could not load input");
        // If we couldn't load the archive, check if it's a directory
        if (viewer.type == SOURCE_DIRECTORY) {
            snprintf(viewer.status_message, sizeof(viewer.status_message),
                     "No images found in folder: %s", path);
            free(viewer.source_path);
            viewer.source_path = NULL;
        }
    }

    return result;
}

void unload_view(ImageView *view) {
    if (!view) return;
    comic_viewer_finish_loading();

    // Unload images for the specified view
    for (int i = 0; i < view->count; i++) {
        int img_index = view->image_indices[i];
        if (img_index >= 0 && img_index < viewer.image_count && img_index < MAX_IMAGES) {
            unload_image(&viewer.images[img_index]);
        }
    }
    if (view->surface) {
        SDL_DestroySurface(view->surface);
        view->surface = NULL;
    }
    // Free the texture if it exists
    if (view->texture) {
        SDL_DestroyTexture(view->texture);
        view->texture = NULL;
    }
}

static void preload_adjacent_view(void) {
    ImageView *current = viewer.current_view_node;
    if (!pending_preload_direction || !current || !current->texture || viewer.load_thread) return;
    ImageView *adjacent = pending_preload_direction < 0 ? current->prev : current->next;
    pending_preload_direction = 0;
    if (adjacent && !adjacent->texture) load_view(adjacent);
}

void comic_viewer_run(void) {
    // get default image processing options
    options = get_default_processing_options();

    if (viewer.image_count > 0) {
        // Load images for the current view
        load_view(viewer.current_view_node);
        // wait for first view to be loaded
        comic_viewer_finish_loading();
        viewer_render_current_view();
        // Async preload images for the next view if available
        if (viewer.current_view_node->next) {
            load_view(viewer.current_view_node->next);
        }
    } else {
        // No images loaded, ensure file browser is open
        if (!file_browser_is_active()) {
             SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No images to display and file browser not active");
             return;
        }
    }

    viewer.running = true;

    // Main loop
    while (viewer.running) {
        linux_desktop_pump();
        // Handle events
        handle_events();

        // Update state based on continuous input (like mouse position)
        update_state();

        // Render the current image
        viewer_render_current_view();
        preload_adjacent_view();
    }

    // Cleanup resources
    comic_viewer_finish_loading();
    free(options);
    options = NULL;
}

void comic_viewer_cleanup(void) {
    linux_desktop_cleanup();
    comic_viewer_finish_loading();
    comic_viewer_save_reading_position();

    // Clean up progress bar resources
    progress_bar_cleanup();
    
    // Clean up image loader backend
    image_loader_cleanup();
    
    // Close archive if open
    if (viewer.archive) {
        archive_close(viewer.archive);
        viewer.archive = NULL;
    }
    // Close gamepad if open
    if (viewer.gamepad) {
        SDL_CloseGamepad(viewer.gamepad);
        viewer.gamepad = NULL;
        viewer.gamepad_id = 0;
    }

    gpu_shader_shutdown();

    imgui_layer_shutdown();

    free_resources();
    SDL_Quit();
}

// Internal helper functions
static bool prepare_image(int index) {
    if (index < 0 || index >= viewer.image_count || index >= MAX_IMAGES) return false;
    
    // If the surface is already loaded, do nothing
    if (viewer.images[index].surface != NULL) return true;

    if (viewer.archive) {
        char *image_path = NULL;
        if (archive_get_image(viewer.archive, index, &image_path)) {
            // Store the path in our image entry
            free(viewer.images[index].path);
            viewer.images[index].path = image_path;
            return true;
        }
        return false;
    } else {
        // Standard loading mode
        return true;
    }
}

static void unload_image(ImageEntry *image) {
    if (image->surface) {
        SDL_DestroySurface(image->surface);
        image->surface = NULL;
    }
}

static void handle_events(void) {
    SDL_Event event;
    
    while (SDL_PollEvent(&event)) {
        imgui_layer_process_event(&event);
        bool browser_active = file_browser_is_active();
        switch (event.type) {
                case SDL_EVENT_USER: {
                    // Handle surface loading completion events
                    if (event.type == viewer.preload_event_type) {
                        PreloadResult *res = (PreloadResult *)event.user.data1;
                        int current_gen = SDL_GetAtomicInt(&viewer.load_generation);
                        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Preload event: res_gen=%d, current_gen=%d", 
                                    res ? res->generation : -1, current_gen);
                        if (res && res->view && res->generation == current_gen) {
                            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Preload valid, creating texture");
                            ImageView *view = res->view;
                            if (viewer.load_thread) {
                                SDL_WaitThread(viewer.load_thread, NULL);
                                viewer.load_thread = NULL; // Clear the thread handle
                            }
                            // Create texture from the loaded surface
                            create_texture(viewer.renderer, view);
                        } else if (res) {
                            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Stale preload ignored: gen=%d vs current=%d", 
                                        res->generation, current_gen);
                        }
                        free(res);
                    }
                }
                break;
            case SDL_EVENT_DROP_FILE:
                file_browser_load_path(event.drop.data);
                break;
            case SDL_EVENT_QUIT:
                viewer.running = false;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (browser_active) {
                    break;
                }
                if (imgui_layer_is_visible() && imgui_layer_wants_capture_mouse()) {
                    break;
                }
                // Mouse wheel for page navigation
                if (event.wheel.y > 0) {  // Scroll up
                    go_to_previous_view();
                } else if (event.wheel.y < 0) {  // Scroll down
                    go_to_next_view();
                }
                break;
                
            

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (browser_active) {
                    break;
                }
                if (imgui_layer_is_visible() && imgui_layer_wants_capture_mouse()) {
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    // Left mouse button pressed - toggle zoom
                    if (viewer.zoomed) {
                        // If already zoomed, return to normal view
                        viewer.zoomed = false;
                        viewer.pan_offset_x = 0;
                        viewer.pan_offset_y = 0;
                    } else {
                        // Zoom in with center at click location
                        viewer.zoomed = true;
                        viewer.zoom_center_x = event.button.x;
                        viewer.zoom_center_y = event.button.y;
                        viewer.zoom_level = 2.0f; // Set zoom level to 200%
                    }
                } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                    // Middle mouse button: enter cropping mode and start drag
                    viewer.cropping_mode = true;
                    viewer.cropping_active = true;
                    viewer.cropping_button = event.button.button;
                    viewer.crop_start_x = event.button.x;
                    viewer.crop_start_y = event.button.y;
                    viewer.crop_current_x = event.button.x;
                    viewer.crop_current_y = event.button.y;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (browser_active) {
                    break;
                }
                if (imgui_layer_is_visible() && imgui_layer_wants_capture_mouse()) {
                    break;
                }
                // Update crop rectangle when dragging in cropping mode
                if (viewer.cropping_mode && viewer.cropping_active) {
                    viewer.crop_current_x = event.motion.x;
                    viewer.crop_current_y = event.motion.y;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (browser_active) {
                    break;
                }
                if (imgui_layer_is_visible() && imgui_layer_wants_capture_mouse()) {
                    break;
                }
                if (viewer.cropping_mode && viewer.cropping_active && event.button.button == viewer.cropping_button) {
                    // Finish cropping
                    viewer.cropping_active = false;
                    // Keep cropping_mode true so overlay remains until user cancels or applies
                }
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (browser_active) {
                    break;
                }
                // Map left/right shoulder to page navigation
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
                    go_to_previous_view();
                } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
                    go_to_next_view();
                }
                break;

            case SDL_EVENT_GAMEPAD_ADDED: {
                // Open the newly added gamepad (device index in event.gdevice.which)
                SDL_JoystickID which = event.gdevice.which;
                // If we already have a gamepad open, close it first
                if (viewer.gamepad) {
                    SDL_CloseGamepad(viewer.gamepad);
                    viewer.gamepad = NULL;
                    viewer.gamepad_id = 0;
                }
                // Try to open the new gamepad by device index
                viewer.gamepad = SDL_OpenGamepad((int)which);
                if (viewer.gamepad) {
                    viewer.gamepad_id = SDL_GetGamepadID(viewer.gamepad);
                    const char *name = SDL_GetGamepadName(viewer.gamepad);
                    snprintf(viewer.gamepad_status_msg, sizeof(viewer.gamepad_status_msg), "Gamepad connected: %s", name ? name : "(unknown)");
                    viewer.gamepad_status_until = SDL_GetTicks() + 3000;
                }
            }
            break;

            case SDL_EVENT_GAMEPAD_REMOVED: {
                SDL_JoystickID which = event.gdevice.which;
                if (viewer.gamepad && viewer.gamepad_id == which) {
                    SDL_CloseGamepad(viewer.gamepad);
                    viewer.gamepad = NULL;
                    viewer.gamepad_id = 0;
                    snprintf(viewer.gamepad_status_msg, sizeof(viewer.gamepad_status_msg), "Gamepad disconnected");
                    viewer.gamepad_status_until = SDL_GetTicks() + 3000;
                }
            }
            break;
                
            case SDL_EVENT_KEY_DOWN:
                if (browser_active) {
                    // Keep browser-only key handling isolated while overlay is open.
                    file_browser_handle_key(event.key.key);
                    break;
                }
                if (event.key.key == SDLK_F2) {
                    linux_desktop_show_controls();
                    break;
                }
                if (event.key.key == SDLK_F1 || event.key.scancode == SDL_SCANCODE_F1) {
                    if (!imgui_layer_is_initialized()) {
                        snprintf(viewer.gamepad_status_msg, sizeof(viewer.gamepad_status_msg), "ImGui overlay unavailable");
                        viewer.gamepad_status_until = SDL_GetTicks() + 2500;
                    } else {
                        bool new_visible = !imgui_layer_is_visible();
                        imgui_layer_set_visible(new_visible);
                        snprintf(viewer.gamepad_status_msg, sizeof(viewer.gamepad_status_msg), "ImGui overlay: %s", new_visible ? "ON" : "OFF");
                        viewer.gamepad_status_until = SDL_GetTicks() + 1500;
                    }
                    break;
                }
                if (event.key.key == SDLK_I || event.key.scancode == SDL_SCANCODE_I) {
                    if (!imgui_layer_is_initialized()) {
                        snprintf(viewer.gamepad_status_msg, sizeof(viewer.gamepad_status_msg), "ImGui overlay unavailable");
                        viewer.gamepad_status_until = SDL_GetTicks() + 2500;
                    } else {
                        imgui_layer_toggle_exif_info();
                        snprintf(viewer.gamepad_status_msg, sizeof(viewer.gamepad_status_msg), "Image info: %s", imgui_layer_is_exif_info_visible() ? "ON" : "OFF");
                        viewer.gamepad_status_until = SDL_GetTicks() + 1500;
                    }
                    break;
                }
                if (imgui_layer_is_visible() && imgui_layer_wants_capture_keyboard()) {
                    break;
                }
                switch (event.key.key) {
                    case SDLK_1:
                    case SDLK_2:
                    case SDLK_P:
                    case SDLK_E:
                    case SDLK_R:
                    case SDLK_X:
                    case SDLK_C:
                    case SDLK_B:
                    case SDLK_G:
                        comic_viewer_finish_loading();
                        break;
                    default:
                        break;
                }
                switch (event.key.key) {
                    case SDLK_S:
                        // Toggle scaling mode (Linear <-> Nearest)
                         if (viewer.scale_mode == SDL_SCALEMODE_LINEAR) {
                            viewer.scale_mode = SDL_SCALEMODE_NEAREST;
                        } else {
                            viewer.scale_mode = SDL_SCALEMODE_LINEAR;
                        }
                        break;
                    case SDLK_DELETE:
                        if (event.key.repeat) break;
                        if (viewer.type == SOURCE_DIRECTORY) {
                            // In a folder, Delete erases the file(s) from disk
                            delete_current_view_from_disk((event.key.mod & SDL_KMOD_SHIFT) != 0);
                        } else {
                            // Remove current view
                            remove_current_view();
                        }
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        // Apply crop if cropping mode, else toggle file browser
                        if (viewer.cropping_mode) {
                            viewer_apply_crop_selection();
                            // Exit cropping mode after apply
                            viewer.cropping_mode = false;
                            viewer.cropping_active = false;
                            break;
                        }
                        // Toggle file browser
                        if (!file_browser_is_active()) {
                            // Open at current comic directory or cwd if none
                            if (viewer.source_path) {
                                // are we already in a directory?
                                if (viewer.type == SOURCE_DIRECTORY) {
                                    file_browser_open(viewer.source_path);
                                } else {
                                    char dir_path[4096];
                                    strncpy(dir_path, viewer.source_path, sizeof(dir_path)-1); dir_path[sizeof(dir_path)-1]='\0';
                                    // strip filename if source_path is a file (has extension)
                                    const char *dot = strrchr(dir_path, '.');
                                    const char *slash = strrchr(dir_path, '/');
                                    if (dot && slash && dot > slash) {
                                        // it's a file path; truncate after slash
                                        dir_path[slash - dir_path] = '\0';
                                    }
                                    file_browser_open(dir_path);
                                }
                            } else {
                                file_browser_open(NULL);
                            }
                        } else {
                            // Already handled above (should not get here while active)
                        }
                        break;
                    case SDLK_ESCAPE:
                        // If cropping mode is active, cancel it; otherwise quit
                        if (viewer.cropping_mode) {
                            viewer.cropping_mode = false;
                            viewer.cropping_active = false;
                            break;
                        } else {
                            viewer.running = false;
                        }
                        break;

                    case SDLK_1:
                        if (!viewer.current_view_node || viewer.current_view_node->count <= 1) {
                            // This view is already in single image mode
                            break;
                        }
                        
                        // Save the second image index before modifying the view
                        int second_image_index = viewer.current_view_node->image_indices[1];
                        
                        // save link to the next view node
                        ImageView *backup_next_view = viewer.current_view_node->next;
                        // insert a new view node after the current one
                        ImageView *new_view = create_view_node_after(viewer.current_view_node);
                        if (!new_view) {
                            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create new view node");
                            break;
                        }
                        viewer.current_view_node->count = 1;
                        viewer.current_view_node->image_indices[1] = -1;
                        new_view->count = 1;
                        new_view->image_indices[0] = second_image_index;
                        new_view->next = backup_next_view;
                        new_view->prev = viewer.current_view_node;
                        viewer.current_view_node->next = new_view;
                        if (backup_next_view) {
                            backup_next_view->prev = new_view;
                        }
                        viewer.view_count++;
                        
                        unload_view(viewer.current_view_node);
                        unload_view(new_view);
                        view_changed(NULL, viewer.current_view_node);

                        break;

                    case SDLK_2: 
                        {
                            if (!viewer.current_view_node || viewer.current_view_node->count != 1 ||
                                !viewer.current_view_node->next || viewer.current_view_node->next->count <= 0) {
                                // This view is already in double image mode or there is no next image to pair with
                                break;
                            }

                            ImageView *old_next_view = viewer.current_view_node->next;
                            unload_view(viewer.current_view_node);
                            unload_view(old_next_view);
                            viewer.current_view_node->count = 2;
                            viewer.current_view_node->image_indices[1] = old_next_view->image_indices[0];
                            for (int index = 1; index < old_next_view->count; index++) {
                                old_next_view->image_indices[index - 1] = old_next_view->image_indices[index];
                            }
                            old_next_view->count--;
                            old_next_view->image_indices[old_next_view->count] = -1;
                            if (old_next_view->count == 0) {
                                viewer.current_view_node->next = old_next_view->next;
                                if (old_next_view->next) {
                                    old_next_view->next->prev = viewer.current_view_node;
                                }
                                viewer.view_count--;
                                free(old_next_view);
                            }
                            view_changed(NULL, viewer.current_view_node);
                        }
                        break;
                        
                    case SDLK_RIGHT:
                    case SDLK_SPACE:
                    case SDLK_DOWN:
                        go_to_next_view();
                        break;
                        
                    case SDLK_LEFT:
                    case SDLK_UP:
                    case SDLK_BACKSPACE:
                        go_to_previous_view();
                        break;
                        
                    case SDLK_HOME:
                        // First image
                        if (get_current_view() != 0) {
                            if (viewer.load_thread && SDL_GetThreadState(viewer.load_thread) == SDL_THREAD_ALIVE) {
                                return;
                            }                            
                            viewer.current_view_index = 0;
                            view_changed(viewer.current_view_node, viewer.first_view);
                        }
                        break;
                        
                    case SDLK_END:
                        // Last image
                        {
                            if (!viewer.first_view) break;
                            if (viewer.load_thread && SDL_GetThreadState(viewer.load_thread) == SDL_THREAD_ALIVE) {
                                return;
                            }                            
                            ImageView *last_view = viewer.first_view;
                            while (last_view->next) {
                                last_view = last_view->next;
                            }
                            viewer.current_view_index = viewer.view_count - 1;
                            view_changed(viewer.current_view_node, last_view);
                        }
                        break;

                    case SDLK_F:
                        // Toggle fullscreen
                        toggle_fullscreen();
                        break;
                        
                    case SDLK_F11:
                        // Toggle fullscreen with F11 key
                        toggle_fullscreen();
                        break;
                        
                    // Zoom controls
                    case SDLK_EQUALS: // Plus key (often requires shift)
                    case SDLK_KP_PLUS: // Numpad plus
                        if (viewer.zoomed) {
                            // Increase zoom level
                            viewer.zoom_level += 0.25f;
                            if (viewer.zoom_level > viewer.max_zoom)
                                viewer.zoom_level = viewer.max_zoom;
                        }
                        break;
                        
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS: // Numpad minus
                        if (viewer.zoomed) {
                            // Decrease zoom level
                            viewer.zoom_level -= 0.25f;
                            if (viewer.zoom_level < 1.0f) {
                                // Exit zoom mode if we go below 100%
                                viewer.zoomed = false;
                                viewer.zoom_level = 1.0f;
                            }
                        }
                        break;
                        
                    case SDLK_Z: // Toggle zoom mode
                        viewer.zoomed = !viewer.zoomed;
                        if (viewer.zoomed) {
                            // When entering zoom mode with keyboard, center on the middle of the window
                            viewer.zoom_center_x = viewer.window_width / 2;
                            viewer.zoom_center_y = viewer.window_height / 2;
                            viewer.zoom_level = 2.0f;
                        }
                        break;

                    case SDLK_O:
                        if (event.key.repeat) break;
                        if (event.key.mod & SDL_KMOD_SHIFT) {
                            // Shift+O: cycle the overlay pixel shader and force the
                            // OVERLAY_PIXEL_SHADER mode so the effect is visible.
                            int next = (int)viewer.overlay_pixel_shader + 1;
                            if (next >= PIXEL_SHADER_COUNT) next = PIXEL_SHADER_SCANLINES;
                            if (next == PIXEL_SHADER_NONE) next = PIXEL_SHADER_SCANLINES;
                            viewer.overlay_pixel_shader = (PixelShaderType)next;
                            viewer.overlay_mode = OVERLAY_PIXEL_SHADER;
                            snprintf(viewer.gamepad_status_msg,
                                     sizeof(viewer.gamepad_status_msg),
                                     "Overlay shader: %s",
                                     pixel_shader_get_name(viewer.overlay_pixel_shader));
                            viewer.gamepad_status_until = SDL_GetTicks() + 1500;
                        } else {
                            // Cycle overlay mode
                            int next = (int)viewer.overlay_mode + 1;
                            if (next >= OVERLAY_MODE_COUNT) next = 0;
                            viewer.overlay_mode = (OverlayMode)next;
                        }
                        viewer.last_page_change_time = SDL_GetTicks();
                        break;

                    case SDLK_P:
                        if (event.key.repeat) break;
                        {
                            // Cycle the view pixel shader; reload current view so
                            // the new filter is applied to the surface.
                            int next = (int)viewer.view_pixel_shader + 1;
                            if (next >= PIXEL_SHADER_COUNT) next = PIXEL_SHADER_NONE;
                            viewer.view_pixel_shader = (PixelShaderType)next;
                            snprintf(viewer.gamepad_status_msg,
                                     sizeof(viewer.gamepad_status_msg),
                                     "View shader: %s",
                                     pixel_shader_get_name(viewer.view_pixel_shader));
                            viewer.gamepad_status_until = SDL_GetTicks() + 1500;
                            unload_view(viewer.current_view_node);
                            load_view(viewer.current_view_node);
                        }
                        break;
                        
                    case SDLK_E: // Toggle image enhancements
                        {
                            options->enhancement_enabled = !options->enhancement_enabled;
                            // Force reload of only the currently visible images to apply/remove enhancements
                            unload_view(viewer.current_view_node);
                            load_view(viewer.current_view_node);
                        }
                        break;

                    case SDLK_R: // Toggle automatic white border removal
                        {
                            viewer.border_removal_enabled = !viewer.border_removal_enabled;

                            snprintf(viewer.gamepad_status_msg,
                                     sizeof(viewer.gamepad_status_msg),
                                     "Border Removal: %s",
                                     viewer.border_removal_enabled ? "On" : "Off");
                            viewer.gamepad_status_until = SDL_GetTicks() + 1500;

                            unload_view(viewer.current_view_node);
                            load_view(viewer.current_view_node);
                        }
                        break;

                    case SDLK_X: // Cycle through color filters
                        {
                            // Cycle to next filter
                            int next_filter = (int)options->color_filter + 1;
                            if (next_filter >= COLOR_FILTER_COUNT) {
                                next_filter = COLOR_FILTER_NONE;
                            }
                            options->color_filter = (ColorFilterType)next_filter;

                            snprintf(viewer.gamepad_status_msg,
                                     sizeof(viewer.gamepad_status_msg),
                                     "Color Filter: %s",
                                     color_filter_get_name(options->color_filter));
                            viewer.gamepad_status_until = SDL_GetTicks() + 1500;

                            unload_view(viewer.current_view_node);
                            load_view(viewer.current_view_node);
                        }
                        break;

                    case SDLK_C:
                        {
                            // If Ctrl+Shift+C -> copy current view instead of adjusting contrast
                            if ((event.key.mod & SDL_KMOD_CTRL) && (event.key.mod & SDL_KMOD_SHIFT)) {
                                copy_current_view_to_clipboard();
                                break;
                            }
                            options->enhancement_enabled = true;
                            if (event.key.mod & SDL_KMOD_SHIFT) {
                                options->contrast -= 5;
                                if (options->contrast < -100) options->contrast = -100;
                            } else {
                                options->contrast += 5;
                                if (options->contrast > 100) options->contrast = 100;
                            }
                            unload_view(viewer.current_view_node);
                            load_view(viewer.current_view_node);
                        }
                        break;

                    case SDLK_B:
                        if (viewer.image_count == 0) {
                            // No images loaded — open the file browser
                            if (!file_browser_is_active()) {
                                file_browser_open(viewer.source_path);
                                imgui_layer_set_status_message(NULL);
                            }
                        } else {
                            options->enhancement_enabled = true;
                            if (event.key.mod & SDL_KMOD_SHIFT) {
                                options->brightness -= 5;
                                if (options->brightness < -100) options->brightness = -100;
                            } else {
                                options->brightness += 5;
                                if (options->brightness > 100) options->brightness = 100;
                            }
                            unload_view(viewer.current_view_node);
                            load_view(viewer.current_view_node);
                        }
                        break;

                    case SDLK_G:
                        {
                            options->enhancement_enabled = true;
                            if (event.key.mod & SDL_KMOD_SHIFT) {
                                options->gamma -= 0.1;
                                if (options->gamma < 0.1) options->gamma = 0.1;
                            } else {
                                options->gamma += 0.1;
                                if (options->gamma > 3.0) options->gamma = 3.0;
                            }
                            unload_view(viewer.current_view_node);
                            load_view(viewer.current_view_node);
                        }
                        break;
                }
                break;
                
            case SDL_EVENT_WINDOW_RESIZED:
                // Update window dimensions
                viewer.window_width = event.window.data1;
                viewer.window_height = event.window.data2;
                
                // Update logical presentation for HiDPI
                SDL_GetWindowSizeInPixels(viewer.window, &viewer.drawable_width, &viewer.drawable_height);
                
                if (viewer.window_width > 0 && viewer.window_height > 0) {
                    SDL_SetRenderLogicalPresentation(viewer.renderer, 
                                                    viewer.window_width, 
                                                    viewer.window_height,
                                                    SDL_LOGICAL_PRESENTATION_LETTERBOX);
                }
                break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                viewer.drawable_width = event.window.data1;
                viewer.drawable_height = event.window.data2;
                break;
                
            case SDL_EVENT_WINDOW_EXPOSED:
                // Window needs to be redrawn
                viewer_render_current_view();
                break;
        }
    }
}

static void update_state(void) {
    if (!viewer.zoomed || file_browser_is_active()) {
        // Reset velocities when not zoomed
        viewer.pan_velocity_x = 0.0f;
        viewer.pan_velocity_y = 0.0f;
        viewer.last_update_ticks = SDL_GetTicks();
        return;
    }

    // Compute delta time in seconds
    Uint64 now = SDL_GetTicks();
    float dt = (now - viewer.last_update_ticks) / 1000.0f;
    if (dt <= 0.0f) dt = 0.001f; // minimum step
    viewer.last_update_ticks = now;

    float mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    const int pan_margin = 60; // pixels from edge to start panning

    // Determine target acceleration direction based on mouse position
    float accel_x = 0.0f;
    float accel_y = 0.0f;

    if (mouse_x < pan_margin) {
        accel_x = viewer.pan_acceleration;
    } else if (mouse_x > viewer.window_width - pan_margin) {
        accel_x = -viewer.pan_acceleration;
    }

    if (mouse_y < pan_margin) {
        accel_y = viewer.pan_acceleration;
    } else if (mouse_y > viewer.window_height - pan_margin) {
        accel_y = -viewer.pan_acceleration;
    }

    // Integrate velocity
    viewer.pan_velocity_x += accel_x * dt;
    viewer.pan_velocity_y += accel_y * dt;

    // If no acceleration on an axis, apply damping towards zero
    if (accel_x == 0.0f) {
        // exponential-like damping
        float damping_factor = 1.0f - viewer.pan_damping * dt;
        if (damping_factor < 0.0f) damping_factor = 0.0f;
        viewer.pan_velocity_x *= damping_factor;
        if (fabsf(viewer.pan_velocity_x) < 1.0f) viewer.pan_velocity_x = 0.0f;
    }
    if (accel_y == 0.0f) {
        float damping_factor = 1.0f - viewer.pan_damping * dt;
        if (damping_factor < 0.0f) damping_factor = 0.0f;
        viewer.pan_velocity_y *= damping_factor;
        if (fabsf(viewer.pan_velocity_y) < 1.0f) viewer.pan_velocity_y = 0.0f;
    }

    // Clamp speeds
    if (viewer.pan_velocity_x > viewer.pan_max_speed) viewer.pan_velocity_x = viewer.pan_max_speed;
    if (viewer.pan_velocity_x < -viewer.pan_max_speed) viewer.pan_velocity_x = -viewer.pan_max_speed;
    if (viewer.pan_velocity_y > viewer.pan_max_speed) viewer.pan_velocity_y = viewer.pan_max_speed;
    if (viewer.pan_velocity_y < -viewer.pan_max_speed) viewer.pan_velocity_y = -viewer.pan_max_speed;

    // Integrate position
    viewer.pan_offset_x += viewer.pan_velocity_x * dt;
    viewer.pan_offset_y += viewer.pan_velocity_y * dt;
}

static void free_resources(void) {
    // iterate over views
    for (ImageView *view = viewer.first_view; view; view = view->next) {
        unload_view(view);
    }

    // Free source path
    free(viewer.source_path);
    viewer.source_path = NULL;
    
    // Free views linked list
    free_all_views();
    
    // Free font resources
    if (viewer.font) {
        TTF_CloseFont(viewer.font);
        viewer.font = NULL;
    }
    
    // Destroy renderer and window
    if (viewer.renderer) {
        SDL_DestroyRenderer(viewer.renderer);
        viewer.renderer = NULL;
    }
    
    if (viewer.window) {
        SDL_DestroyWindow(viewer.window);
        viewer.window = NULL;
    }
}

// Helper function to clean up thread resources on early exit (when thread becomes stale)
static int cleanup_load_thread_resources(SDL_Surface *combined_surface) {
    if (combined_surface) {
        SDL_DestroySurface(combined_surface);
    }
    return 0;
}

// Thread: decode/prepare surfaces only, then post event to main thread to create texture
static int load_view_surfaces_in_thread(void *data) {
    LoadViewTask *task = (LoadViewTask *)data;
    ImageView *view = task->view;
    int generation = task->generation;
    free(task);

    // If the image is already loaded, skip processing
    if (view->texture) return 0;

    int total_width = 0;
    int max_height = 0;

    SDL_Surface *surface = NULL;
    SDL_Surface *combined_surface = NULL;  // track the combined surface for cleanup
    for (int i = 0; i < view->count; i++) {

        // check if the running thread is still the active load thread for the viewer (not canceled by user switching page)
        if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
            return cleanup_load_thread_resources(combined_surface);
        }

        int image_index = view->image_indices[i];
        if (!prepare_image(image_index)) return -1;
        ImageEntry *image = &viewer.images[image_index];

        if (!image->surface) {
            image->surface = image_load_surface(image->path, options);
            if (!image->surface) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Failed to load image %s using backend %s",
                             image->path,
                             image_loader_backend_name(image_loader_get_active_backend()));
                return -1;
            }
        }

        // Calculate total width and max height for the texture
        total_width += image->surface->w;
        if (image->surface->h > max_height) {
            max_height = image->surface->h;
        }

        surface = image->surface;
    }

    // check if the running thread is still the active load thread for the viewer (not canceled by user switching page)
    if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
        return cleanup_load_thread_resources(combined_surface);
    }

    if (view->count > 1) {
        // create a new surface for the combined texture
        surface = SDL_CreateSurface(total_width, max_height, surface->format);
        if (!surface) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create combined surface: %s", SDL_GetError());
            return -1;
        }
        combined_surface = surface;  // track for cleanup

        SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
        int current_x = 0;
        for (int i = 0; i < view->count; i++) {

            if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
                return cleanup_load_thread_resources(combined_surface);
            }

            int image_index = view->image_indices[i];
            ImageEntry *image = &viewer.images[image_index];
            // Copy the image surface to the combined surface
            SDL_Rect dst_rect = {current_x, 0, image->surface->w, image->surface->h};
            if (!SDL_BlitSurface(image->surface, NULL, surface, &dst_rect)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to blit image %s: %s", image->path, SDL_GetError());
                SDL_DestroySurface(surface);
                return -1;
            }

            current_x += image->surface->w;
        }
    }

    if (view->count == 1) {
        surface = SDL_DuplicateSurface(surface);
        if (!surface) return -1;
        combined_surface = surface;
    }

    if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
        return cleanup_load_thread_resources(combined_surface);
    }    

    SDL_FRect crop_rect = {0, 0, (float)surface->w, (float)surface->h};
    if (viewer.border_removal_enabled) {
        // Detect and crop white borders
        int left = 0, right = surface->w - 1;
        int top = 0, bottom = surface->h - 1;
        int threshold = viewer.border_white_threshold;
        int required_non_white = viewer.border_required_non_white;
        if (threshold < 0) threshold = 0;
        if (threshold > 255) threshold = 255;
        if (required_non_white < 1) required_non_white = 1;
        
        // Analyze pixels to detect borders
        uint8_t *pixels = (uint8_t*)surface->pixels;
        int pitch = surface->pitch;
        const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(surface->format);
        int bpp = details->bytes_per_pixel;

        SDL_Palette *palette = SDL_GetSurfacePalette(surface);
        if (bpp == 3 || bpp == 4) {
            palette = NULL; // For 24/32-bit images, use the pixel format directly
        } else {
            palette = SDL_GetSurfacePalette(surface);
        }

        uint8_t *p;
        // Scan from left edge inward
        for (left = 0; left < surface->w / 2; left++) {
            int non_white_count = 0;
            
            for (int y = 0; y < surface->h; y += 2) { // Sample every other pixel for speed
                uint32_t pixel = 0;
                p = pixels + y * pitch + left * bpp;
                
                uint8_t r, g, b, a;

                if (!palette) {
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        r = p[0] << 16;
                        g = p[1] << 8;
                        b = p[2];
                    #else
                        r = p[0]; g = p[1]; b = p[2];
                    #endif
                } else {
                    switch (bpp) {
                        case 1: pixel = *p; break;
                        case 2: pixel = *(uint16_t*)p; break;
                    }
                    SDL_GetRGBA(pixel, details, palette, &r, &g, &b, &a);
                }
               
                // If pixel is not "white" (using average of RGB values)
                int avg = (r + g + b) / 3;
                if (avg < threshold) {
                    non_white_count++;
                    if (non_white_count >= required_non_white) {
                        break;
                    }
                }
            }
            
            if (non_white_count >= required_non_white) {
                break; // Found non-white content
            }
        }

        if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
            return cleanup_load_thread_resources(combined_surface);
        }
        
        // Scan from right edge inward
        for (right = surface->w - 1; right > left + 100; right--) { // Ensure min width
            int non_white_count = 0;
            
            for (int y = 0; y < surface->h; y += 2) {
                uint32_t pixel = 0;
                p = pixels + y * pitch + right * bpp;
                
                uint8_t r, g, b, a;

                if (!palette) {
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        r = p[0] << 16;
                        g = p[1] << 8;
                        b = p[2];
                    #else
                        r = p[0]; g = p[1]; b = p[2];
                    #endif
                } else {
                    switch (bpp) {
                        case 1: pixel = *p; break;
                        case 2: pixel = *(uint16_t*)p; break;
                    }
                    SDL_GetRGBA(pixel, details, palette, &r, &g, &b, &a);
                }
               
                // If pixel is not "white" (using average of RGB values)           
                int avg = (r + g + b) / 3;
                if (avg < threshold) {
                    non_white_count++;
                    if (non_white_count >= required_non_white) {
                        break;
                    }
                }
            }
            
            if (non_white_count >= required_non_white) {
                break; // Found non-white content
            }
        }

        if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
            return cleanup_load_thread_resources(combined_surface);
        }
        
        // Scan from top edge down
        for (top = 0; top < surface->h - 50; top++) {
            int non_white_count = 0;
            
            for (int x = left; x <= right; x += 2) {
                uint32_t pixel = 0;
                
                uint8_t r, g, b, a;

                p = pixels + top * pitch + x * bpp;

                if (!palette) {
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        r = p[0] << 16;
                        g = p[1] << 8;
                        b = p[2];
                    #else
                        r = p[0]; g = p[1]; b = p[2];
                    #endif
                } else {
                    switch (bpp) {
                        case 1: pixel = *p; break;
                        case 2: pixel = *(uint16_t*)p; break;
                    }
                    SDL_GetRGBA(pixel, details, palette, &r, &g, &b, &a);
                }
                
                int avg = (r + g + b) / 3;
                if (avg < threshold) {
                    non_white_count++;
                    if (non_white_count >= required_non_white) {
                        break;
                    }
                }
            }
            
            if (non_white_count >= required_non_white) {
                break; // Found non-white content
            }
        }

        if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
            return cleanup_load_thread_resources(combined_surface);
        }
        
        // Scan from bottom edge up
        for (bottom = surface->h - 1; bottom > top + 100; bottom--) { // Ensure min height
            int non_white_count = 0;

            for (int x = left; x <= right; x += 2) {
                       
                uint8_t r, g, b, a;

                p = pixels + bottom * pitch + x * bpp;

                if (!palette) {
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        r = p[0] << 16;
                        g = p[1] << 8;
                        b = p[2];
                    #else
                        r = p[0]; g = p[1]; b = p[2];
                    #endif
                } else {
                    uint32_t pixel = 0;
                    switch (bpp) {
                        case 1: pixel = *p; break;
                        case 2: pixel = *(uint16_t*)p; break;
                    }
                    SDL_GetRGBA(pixel, details, palette, &r, &g, &b, &a);
                }
                
                int avg = (r + g + b) / 3;
                if (avg < threshold) {
                    non_white_count++;
                    if (non_white_count >= required_non_white) {
                        break;
                    }
                }
            }
            
            if (non_white_count >= required_non_white) {
                break; // Found non-white content
            }
        }

        if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
            return cleanup_load_thread_resources(combined_surface);
        }

        crop_rect = (SDL_FRect){left, top, right - left + 1, bottom - top + 1};
    }

    if (crop_rect.w <= 0 || crop_rect.h <= 0) {
        // reset crop rect to full image size
        crop_rect.x = 0;
        crop_rect.y = 0;
        crop_rect.w = surface->w;
        crop_rect.h = surface->h;
    }
    view->crop_rect = crop_rect;
   
    // Analyze edges
    analyze_image_left_edge(surface, &view->crop_rect, &view->left_edge_color);
    if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
        return cleanup_load_thread_resources(combined_surface);
    }
    analyze_image_right_edge(surface, &view->crop_rect, &view->right_edge_color);
    if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
        return cleanup_load_thread_resources(combined_surface);
    }

    // Apply optional pixel shader filter to the view surface.
    // Done after edge analysis so the gradient overlay uses the original edge
    // colors; done in the worker thread so the main thread doesn't stall.
    if (viewer.view_pixel_shader != PIXEL_SHADER_NONE) {
        pixel_shader_apply_to_surface(viewer.view_pixel_shader, surface);
        if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
            return cleanup_load_thread_resources(combined_surface);
        }
    }

    if (generation != SDL_GetAtomicInt(&viewer.load_generation)) {
        return cleanup_load_thread_resources(combined_surface);
    }
    view->surface = surface; // transfer ownership to main thread

    // Post to main thread to convert surface -> texture
    if (surface) {
        PreloadResult *res = malloc(sizeof(PreloadResult));
        if (res) {
            res->view = view;
            res->generation = generation;

            SDL_Event event;
            SDL_zero(event);
            event.type = viewer.preload_event_type;
            event.user.data1 = res;
            event.user.data2 = NULL;
            if (!SDL_PushEvent(&event)) free(res);
        }
    }

    return 0;
}

// Start worker thread to decode surfaces for the given view
static void load_view(ImageView *view) {
    if (!view) return;
    comic_viewer_finish_loading();
    if (view->texture) return; // already done
    if (view->surface) {
        create_texture(viewer.renderer, view);
        return;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "load_view called for view %p", (void*)view);
    int generation = SDL_GetAtomicInt(&viewer.load_generation);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Starting load task with generation=%d", generation);
    LoadViewTask *task = malloc(sizeof(LoadViewTask));
    if (!task) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate load task");
        return;
    }
    task->view = view;
    task->generation = generation;
    viewer.load_thread = SDL_CreateThread(load_view_surfaces_in_thread, "load_view_thread", (void*)task);
    if (!viewer.load_thread) {
        free(task);
    }
}

static void toggle_fullscreen(void) {
    viewer.fullscreen = !viewer.fullscreen;
    
    if (viewer.fullscreen) {
        // In SDL3, fullscreen is set with SDL_SetWindowFullscreen(window, SDL_TRUE)
        SDL_SetWindowFullscreen(viewer.window, true);
    } else {
        SDL_SetWindowFullscreen(viewer.window, false);
        
        // When exiting fullscreen, ensure window goes back to the correct monitor
        if (viewer.monitor_index >= 0) {
            int x, y;
            if (select_monitor(viewer.monitor_index, &x, &y)) {
                // Position the window at the center of the selected monitor
                SDL_Rect bounds;
                if (SDL_GetDisplayBounds(viewer.monitor_index, &bounds) == 0) {
                    int center_x = bounds.x + (bounds.w - viewer.window_width) / 2;
                    int center_y = bounds.y + (bounds.h - viewer.window_height) / 2;
                    SDL_SetWindowPosition(viewer.window, center_x, center_y);
                } else {
                    // Fall back to the monitor origin if we can't get bounds
                    SDL_SetWindowPosition(viewer.window, x, y);
                }
            }
        }
    }
    // Update drawable size after toggling fullscreen
    SDL_GetWindowSizeInPixels(viewer.window, &viewer.drawable_width, &viewer.drawable_height);
    SDL_GetWindowSize(viewer.window, &viewer.window_width, &viewer.window_height);

    // Re-apply the logical size after toggling fullscreen to maintain HiDPI settings
    if (viewer.renderer) {       
        // Only set logical size if there's a difference (HiDPI)
        if (viewer.drawable_width != viewer.window_width || viewer.drawable_height != viewer.window_height) {
            SDL_SetRenderLogicalPresentation(viewer.renderer, 
                                            viewer.window_width, 
                                            viewer.window_height,
                                            SDL_LOGICAL_PRESENTATION_LETTERBOX);
        }
    }
}

// Public helpers
void viewer_init_view(ImageView *view) { load_view(view); }
bool viewer_has_current_view(void) { return viewer.current_view_node != NULL; }

// Function to select monitor and get its position
static bool select_monitor(int monitor_index, int *x, int *y) {
    int num_displays = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&num_displays);
    if (!displays || num_displays <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No video displays available: %s", SDL_GetError());
        SDL_free(displays);
        return false;
    }

    if (monitor_index < -1 || monitor_index >= num_displays || !x || !y) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid monitor index: %d", monitor_index);
        SDL_free(displays);
        return false;
    }
    SDL_DisplayID display = monitor_index == -1 ? SDL_GetPrimaryDisplay() : displays[monitor_index];
    SDL_free(displays);

    SDL_Rect bounds;
    if (!SDL_GetDisplayBounds(display, &bounds)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get display bounds for monitor %d: %s", monitor_index, SDL_GetError());
        return false;
    }

    *x = bounds.x;
    *y = bounds.y;
    return true;
}

static void create_texture(SDL_Renderer *renderer, ImageView *view) {
    view->texture = SDL_CreateTextureFromSurface(renderer, view->surface);
    if (!view->texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture: %s", SDL_GetError());
    } else if (initial_view_pending && view == viewer.current_view_node) {
        initial_view_pending = false;
        progress_bar_update(1.0f, "Page ready");
    }
}

// Helper function for progress callback
static void update_progress(float progress, const char *message) {
    progress_bar_update(0.05f + 0.7f * progress, message);
}

static void view_changed(ImageView *old_view_node, ImageView *new_view_node) {    
    if (!new_view_node) return;
    comic_viewer_finish_loading();

    // if zoom is active, set zoom y to 0 on the next page
    if (viewer.zoomed) {
        viewer.zoom_center_y = 0;
        viewer.pan_offset_y = 0;
    }

    // Update the page change timer
    viewer.last_page_change_time = SDL_GetTicks();
    viewer.show_progress_indicator = true;
    
    if (old_view_node && old_view_node != new_view_node && old_view_node != new_view_node->next) {
        unload_view(old_view_node);
    }
    
    viewer.current_view_node = new_view_node;
    pending_preload_direction = old_view_node && old_view_node == new_view_node->next ? -1 : 1;

    // Start loading the new view if not already loaded
    if (!new_view_node->texture) {
        load_view(new_view_node);
    }
}

void go_to_previous_view() {
    if (!viewer.current_view_node || !viewer.current_view_node->prev) {
        return;
    }
    
    // Allow instant page change - no blocking on load thread
    viewer.current_view_index--;
    view_changed(viewer.current_view_node, viewer.current_view_node->prev);
}

void go_to_next_view() {
    if (!viewer.current_view_node || !viewer.current_view_node->next) {
        return;
    }
    
    // Allow instant page change - no blocking on load thread
    viewer.current_view_index++;
    view_changed(viewer.current_view_node, viewer.current_view_node->next);
}

/*
 * Generate default views for the comic viewer.
 */
static void generate_default_views() {
    // Clear any existing views if needed
    free_all_views();
    
    viewer.view_count = 0;
    int i = 0;
    
    ImageView *prev_view = NULL;
    while (i < viewer.image_count) {
        ImageView *view = create_view_node_after(prev_view);
        if (!view) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create view node");
            return;
        }
        
        // Single image view by default
        view->image_indices[0] = i;

        prev_view = view;
        
        append_view(view);
        i++;
    }
    
    // Set the current view to the first view
    if (viewer.first_view) {
        viewer.current_view_node = viewer.first_view;
        viewer.current_view_index = 0;
    }
}

// Internal helper functions

// Linked list helper functions
static ImageView* create_view_node_after(ImageView* prev_view) {
    ImageView *view = malloc(sizeof(ImageView));
    if (!view) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for view node");
        return NULL;
    }
    
    // Initialize the view
    view->count = 1; // Default to one image per view
    view->crop_rect = (SDL_FRect){0, 0, 0, 0};
    view->surface = NULL;
    view->next = NULL;
    view->prev = prev_view;
    view->texture = NULL;

    for (int i = 0; i < MAX_IMAGES_PER_VIEW; i++) {
        view->image_indices[i] = -1;
    }
    
    return view;
}

static void append_view(ImageView *view) {
    if (!view) return;
    
    if (!viewer.first_view) {
        // First view in the list
        viewer.first_view = view;
        viewer.current_view_node = view;
    } else {
        // Find the last view and append
        ImageView *current = viewer.first_view;
        while (current->next) {
            current = current->next;
        }
        current->next = view;
        view->prev = current;
    }
    viewer.view_count++;
}

static void free_all_views(void) {
    ImageView *current = viewer.first_view;
    while (current) {
        ImageView *next = current->next;
        free(current);
        current = next;
    }
    viewer.first_view = NULL;
    viewer.current_view_node = NULL;
    viewer.view_count = 0;
}