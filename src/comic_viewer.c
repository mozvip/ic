/**
 * comic_viewer.c
 * Implementation of the comic viewer functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL_hints.h>

#include "comic_viewer.h"
#include "comic_loaders.h"
#include "progress_bar.h"
#include "progress_indicator.h" // Moved here
#include "image_loader.h" // Add FreeImage loader

SDL_Color white = {255, 255, 255, 255}; // White

struct ViewerState viewer; // Define the global viewer variable

// Global state for image enhancement options
ImageProcessingOptions* options;

// Forward declarations for internal functions
static void free_resources(void);
static void handle_events(void);
static void render_current_view(void);
static void display_info();
static bool load_image(int index);
static void unload_image(int index);
static void toggle_fullscreen(void);
static SDL_Color get_dominant_color(SDL_Surface *surface, int x, int y, int width, int height);
static void analyze_image_left_edge(int index, SDL_Color *left_color);
static void analyze_image_right_edge(int index, SDL_Color *right_color);
static SDL_Texture* render_text(const char *text, SDL_Color color);
static bool select_monitor(int monitor_index, int *x, int *y);
static void create_texture(SDL_Renderer *renderer, ImageEntry *image);
static void update_progress(float progress, const char *message);
static void generate_default_views(void);
static void previous_view(void);
static void next_view(void);

// Linked list helper functions
static ImageView* create_view_node(ImageView *prev_view);
static void append_view(ImageView *view);
static void free_all_views(void);
static ImageView* get_view_by_index(int index);
static int get_current_view_index(void);

// Compatibility functions for the linked list implementation
static void set_current_view(int index) {
    ImageView *view = get_view_by_index(index);
    if (view) {
        viewer.current_view_node = view;
        viewer.current_view_index = index;
    }
}

static int get_current_view(void) {
    return viewer.current_view_index;
}

static int get_view_count(void) {
    return viewer.view_count;
}

// Helper function to convert RGB to HSL
// r, g, b, s, l are in [0, 1], h is in [0, 360)
static void rgb_to_hsl(float r, float g, float b, float *h, float *s, float *l) {
    float max_val = fmaxf(fmaxf(r, g), b);
    float min_val = fminf(fminf(r, g), b);
    *l = (max_val + min_val) / 2.0f;

    if (max_val == min_val) {
        *h = 0; // achromatic
        *s = 0;
    } else {
        float d = max_val - min_val;
        *s = (*l > 0.5f) ? d / (2.0f - max_val - min_val) : d / (max_val + min_val);
        if (max_val == r) {
            *h = (g - b) / d + (g < b ? 6.0f : 0);
        } else if (max_val == g) {
            *h = (b - r) / d + 2.0f;
        } else { // max_val == b
            *h = (r - g) / d + 4.0f;
        }
        *h /= 6.0f;
        *h *= 360.0f;
    }
}

// Helper for hsl_to_rgb
static float hue_to_rgb_component(float p, float q, float t) {
    if (t < 0) t += 1.0f;
    if (t > 1) t -= 1.0f;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2.0f) return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

// Helper function to convert HSL to RGB
// r, g, b, s, l are in [0, 1], h is in [0, 360)
static void hsl_to_rgb(float h, float s, float l, float *r, float *g, float *b) {
    if (s == 0) {
        *r = *g = *b = l; // achromatic
    } else {
        float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        float h_norm = h / 360.0f;
        *r = hue_to_rgb_component(p, q, h_norm + 1.0f/3.0f);
        *g = hue_to_rgb_component(p, q, h_norm);
        *b = hue_to_rgb_component(p, q, h_norm - 1.0f/3.0f);
    }
}

// Function to render a horizontal gradient using HSL interpolation
static void render_horizontal_gradient_hsl(SDL_Renderer *renderer, SDL_FRect rect, SDL_Color edge_color_rgb, bool edge_color_is_on_left_of_fill) {
    if (rect.w <= 0) return; // Do not render if width is zero or negative

    float r_edge = edge_color_rgb.r / 255.0f;
    float g_edge = edge_color_rgb.g / 255.0f;
    float b_edge = edge_color_rgb.b / 255.0f;

    float h_edge, s_edge, l_edge;
    rgb_to_hsl(r_edge, g_edge, b_edge, &h_edge, &s_edge, &l_edge);

    for (int col = 0; col < (int)rect.w; ++col) {
        float t; // Interpolation factor: 0 for edge_color, 1 for black
        if (edge_color_is_on_left_of_fill) { // Gradient from left (edge_color) to right (black)
            t = (float)col / (float)(rect.w > 1 ? rect.w -1 : 1); // Avoid division by zero if rect.w is 1
        } else { // Gradient from right (edge_color) to left (black)
            t = 1.0f - ((float)col / (float)(rect.w > 1 ? rect.w -1 : 1));
        }
        // Clamp t to [0, 1] just in case
        t = fmaxf(0.0f, fminf(1.0f, t));


        // Interpolate S and L towards 0 (black), keep H constant
        float s_interp = s_edge * (1.0f - t);
        float l_interp = l_edge * (1.0f - t);

        float r_interp, g_interp, b_interp;
        hsl_to_rgb(h_edge, s_interp, l_interp, &r_interp, &g_interp, &b_interp);

        SDL_SetRenderDrawColor(renderer,
                               (Uint8)(r_interp * 255.0f),
                               (Uint8)(g_interp * 255.0f),
                               (Uint8)(b_interp * 255.0f),
                               255);
        SDL_RenderLine(renderer, rect.x + col, rect.y, rect.x + col, rect.y + rect.h -1.0f);
    }
}


bool comic_viewer_init(int monitor_index) {
    // Set the video driver hint to Wayland before initializing SDL
    if (SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland") == 0) {
        fprintf(stderr, "Warning: Failed to set Wayland video driver hint: %s\n", SDL_GetError());
    }
    
    // Enable HiDPI scaling
    SDL_SetHint("SDL_WINDOW_ALLOW_HIGHDPI", "1");
    
    // Set best quality for scaling operations
    SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "2");  // 0=nearest, 1=linear, 2="best"
    
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return false;
    }
   
    // Initialize SDL_ttf
    if (!TTF_Init()) {
        fprintf(stderr, "SDL_ttf could not initialize! SDL_ttf Error: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // Initialize FreeImage
    if (!image_loader_init()) {
        fprintf(stderr, "Failed to initialize FreeImage library\n");
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    // Determine monitor position
    int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    if (!select_monitor(monitor_index, &x, &y)) {
        fprintf(stderr, "Failed to select monitor %d\n", monitor_index);
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    // Create window
    viewer.window_width = 1024;
    viewer.window_height = 768;
    viewer.monitor_index = monitor_index;
    
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
    viewer.window = SDL_CreateWindow("IC - Image Comic Viewer", 
                                    viewer.window_width, 
                                    viewer.window_height,
                                    window_flags);
                                    
    if (viewer.window == NULL) {
        fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    // Position the window on the selected monitor
    SDL_SetWindowPosition(viewer.window, x, y);

    // get names of all available renderers
    int num_renderers = SDL_GetNumRenderDrivers();
    for (int i = 0; i < num_renderers; i++) {
        const char *render_driver = SDL_GetRenderDriver(i);
        printf("Renderer %d: %s\n", i, render_driver);
    }
    // Set the renderer to use the best available driver
    const char *renderer_name = SDL_GetHint("SDL_RENDER_DRIVER");
    printf("Using renderer: %s\n", renderer_name);

    // Create renderer with enhanced quality settings
    viewer.renderer = SDL_CreateRenderer(viewer.window, renderer_name);
    
    if (viewer.renderer == NULL) {
        fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(viewer.window);
        TTF_Quit();
        SDL_Quit();
        return false;
    }
    
    // Initialize the progress bar
    if (!progress_bar_init(viewer.renderer)) {
        fprintf(stderr, "Warning: Failed to initialize progress bar. Loading will proceed without visual feedback.\n");
        // Continue without progress bar - non-fatal
    }
    
    // Get the actual window size and drawable size for HiDPI scaling
    SDL_GetWindowSizeInPixels(viewer.window, &viewer.drawable_width, &viewer.drawable_height);
    SDL_GetWindowSize(viewer.window, &viewer.window_width, &viewer.window_height);
    
    // Set logical size to handle HiDPI scaling automatically
    if (viewer.drawable_width > viewer.window_width || viewer.drawable_height > viewer.window_height) {
        SDL_SetRenderLogicalPresentation(viewer.renderer, 
                                         viewer.window_width, 
                                         viewer.window_height,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        printf("HiDPI detected: Window size: %dx%d, Drawable size: %dx%d\n", 
               viewer.window_width, viewer.window_height, 
               viewer.drawable_width, viewer.drawable_height);
    }

    // Load font - try to load a system font if available
    viewer.font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20);
    if (!viewer.font) {
        // Try another common font location
        viewer.font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 20);
    }
    if (!viewer.font) {
        // Try another common font location
        viewer.font = TTF_OpenFont("/usr/share/fonts/dejavu/DejaVuSans.ttf", 20);
    }
    if (!viewer.font) {
        fprintf(stderr, "Warning: Failed to load font: %s\n", SDL_GetError());
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
    
    // Initialize progress indicator timer
    viewer.last_page_change_time = 0;
    viewer.show_progress_indicator = false;

    // Initialize zoom settings
    viewer.zoom_level = 1.0f;
    viewer.zoomed = false;
    viewer.zoom_center_x = 0;
    viewer.zoom_center_y = 0;
    viewer.max_zoom = 3.0f;

    for (int i = 0; i < MAX_IMAGES; i++) {
        viewer.images[i].path = NULL;
        viewer.images[i].surface = NULL;
        viewer.images[i].texture = NULL;
        viewer.images[i].width = 0;
        viewer.images[i].height = 0;
    }

    return true;
}

bool comic_viewer_load(const char *path) {
    if (path == NULL) return false;

    // Store the source path
    viewer.source_path = strdup(path);
    if (viewer.source_path == NULL) return false;

    // Initial progress update
    update_progress(0.0f, "Detecting file type...");

    // Check if path is a directory
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        fprintf(stderr, "Cannot access path: %s\n", path);
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
        // generate default views
        generate_default_views();
    } else {
        update_progress(1.0f, "Could not load input");
        // If we couldn't load the archive, check if it's a directory
        if (viewer.type == SOURCE_DIRECTORY) {
            free(viewer.source_path);
            viewer.source_path = NULL;
        }
    }

    return result;
}

void unload_images_for_view(int view_index) {
    ImageView *view = get_view_by_index(view_index);
    if (!view) return;

    // Unload images for the specified view
    for (int i = 0; i < view->count; i++) {
        int img_index = view->image_indices[i];
        unload_image(img_index);
    }
}

void load_images_for_view(int view_index) {
    ImageView *view = get_view_by_index(view_index);
    if (!view) return;

    // Load images for the current view
    for (int i = 0; i < view->count; i++) {
        int img_index = view->image_indices[i];
        if (!load_image(img_index)) {
            fprintf(stderr, "Failed to load image %d\n", img_index);
            return;
        }
    }
}

void comic_viewer_run(void) {
    if (viewer.image_count == 0) {
        fprintf(stderr, "No images to display\n");
        return;
    }

    // get default image processing options
    options = get_default_processing_options();

    // Load images for the current view
    load_images_for_view(get_current_view());
    // Preload images for the next view if available
    if (get_view_count() > 1) {
        load_images_for_view(get_current_view() + 1);
    }
    viewer.running = true;

    // Main loop
    while (viewer.running) {
        // Handle events
        handle_events();

        // Render the current image
        render_current_view();

        // Delay to reduce CPU usage
        SDL_Delay(10);
    }

    // Cleanup resources
    free(options);
}

void comic_viewer_cleanup(void) {
    // Clean up progress bar resources
    progress_bar_cleanup();
    
    // Clean up FreeImage
    image_loader_cleanup();
    
    // Close archive if open
    if (viewer.archive) {
        archive_close(viewer.archive);
        viewer.archive = NULL;
    }
    
    free_resources();
    SDL_Quit();
}

// Internal helper functions
static bool load_image(int index) {
    if (index < 0 || index >= viewer.image_count) return false;
    
    // If the texture is already loaded, do nothing
    if (viewer.images[index].texture != NULL) return true;
    
    if (viewer.archive) {
        char *image_path = NULL;
        if (archive_get_image(viewer.archive, index, &image_path)) {
            // Store the path in our image entry
            viewer.images[index].path = image_path;
            
            // Load the image using our high-quality scaling function
            create_texture(viewer.renderer, &viewer.images[index]);
            if (!viewer.images[index].texture) {
                fprintf(stderr, "Failed to load image %s: %s\n", image_path, SDL_GetError());
                return false;
            }
            
            // Store original dimensions
            SDL_GetTextureSize(viewer.images[index].texture, &viewer.images[index].width, &viewer.images[index].height);
            
            return true;
        }
        return false;
    } else {
        // Standard loading mode
        create_texture(viewer.renderer, &viewer.images[index]);
        if (!viewer.images[index].texture) {
            fprintf(stderr, "Failed to load image %s: %s\n", 
                    viewer.images[index].path, SDL_GetError());
            return false;
        }
        
        // Store original dimensions
        SDL_GetTextureSize(viewer.images[index].texture, &viewer.images[index].width, &viewer.images[index].height);
        
        return true;
    }
}

static void unload_image(int index) {
    if (index < 0 || index >= viewer.image_count) return;
    
    if (viewer.images[index].texture) {
        SDL_DestroyTexture(viewer.images[index].texture);
        viewer.images[index].texture = NULL;
    }
    
    // In on-demand mode, we can also free the path to save memory
    // (it will be re-extracted if needed)
    if (viewer.archive && viewer.images[index].path) {
        free(viewer.images[index].path);
        viewer.images[index].path = NULL;
    }
}

static void handle_events(void) {
    SDL_Event event;
    
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                viewer.running = false;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                // Mouse wheel for page navigation
                if (event.wheel.y > 0) {  // Scroll up
                    previous_view();
                } else if (event.wheel.y < 0) {  // Scroll down
                    next_view();
                }
                break;
                
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    // Left mouse button pressed - toggle zoom
                    if (viewer.zoomed) {
                        // If already zoomed, return to normal view
                        viewer.zoomed = false;
                    } else {
                        // Zoom in with center at click location
                        viewer.zoomed = true;
                        viewer.zoom_center_x = event.button.x;
                        viewer.zoom_center_y = event.button.y;
                        viewer.zoom_level = 2.0f; // Set zoom level to 200%
                    }
                }
                break;
                
            case SDL_EVENT_KEY_DOWN:
                switch (event.key.key) {
                    case SDLK_ESCAPE:
                        viewer.running = false;
                        break;

                    case SDLK_1:
                        if (viewer.current_view_node && viewer.current_view_node->count == 1) {
                            // This view is already in single image mode
                            break;
                        }
                        // the current view now has 1 image
                        viewer.current_view_node->count = 1;

                        // save link to the next view node
                        ImageView *backup_next_view = viewer.current_view_node->next;
                        // insert a new view node after the current one
                        viewer.current_view_node->next = create_view_node(viewer.current_view_node);
                        viewer.current_view_node->next->image_indices[0] = viewer.current_view_node->image_indices[1];
                        viewer.current_view_node->next->next = backup_next_view;
                        if (backup_next_view) {
                            backup_next_view->prev = viewer.current_view_node->next;
                        }

                        break;

                    case SDLK_2:
                        if (viewer.current_view_node && viewer.current_view_node->count == 2) {
                            // This view is already in double image mode
                            break;
                        }
                        // check if we are not already displaying the last image
                        if (viewer.current_view_node && viewer.current_view_node->next) {
                            // the current view now has 2 images
                            viewer.current_view_node->count = 2;
                            // the second image of the current view is the first image of the next view
                            viewer.current_view_node->image_indices[1] = viewer.current_view_node->next->image_indices[0];
                            // ensure the image is loaded
                            if (viewer.images[viewer.current_view_node->image_indices[1]].texture == NULL) {
                                load_image(viewer.current_view_node->image_indices[1]);
                            }
                            // remove the next view from the linked list
                            ImageView *next_view = viewer.current_view_node->next;
                            if (next_view) {
                                viewer.current_view_node->next = next_view->next;
                                if (next_view->next) {
                                    next_view->next->prev = viewer.current_view_node;
                                    viewer.current_view_node->next = next_view->next;
                                }
                            }
                        }
                        break;
                        
                    case SDLK_RIGHT:
                    case SDLK_SPACE:
                    case SDLK_DOWN:
                        next_view();
                        break;
                        
                    case SDLK_LEFT:
                    case SDLK_UP:
                    case SDLK_BACKSPACE:
                        previous_view();
                        break;
                        
                    case SDLK_HOME:
                        // First image
                        if (get_current_view() != 0) {
                            // Clean up any loaded textures except the first one
                            int view_count = get_view_count();
                            for (int i = 1; i < view_count; i++) {
                                unload_images_for_view(i);
                            }
                            set_current_view(0);
                            load_images_for_view(get_current_view());

                            // Preload the next image
                            if (view_count > 1) {
                                load_images_for_view(get_current_view() + 1);
                            }
                        }
                        break;
                        
                    case SDLK_END:
                        // Last image
                        {
                            int view_count = get_view_count();
                            if (get_current_view() != view_count - 1) {
                                // Clean up any loaded textures except the last one
                                for (int i = 0; i < view_count - 1; i++) {
                                    unload_images_for_view(i);
                                }
                                set_current_view(view_count - 1);
                                load_images_for_view(get_current_view());

                                // Preload the previous image
                                if (view_count > 1) {
                                    load_images_for_view(get_current_view() - 1);
                                }
                            }
                        }
                        break;

                    case SDLK_F:
                        // Toggle fullscreen
                        toggle_fullscreen();
                        break;
                        
                    case SDLK_F12:
                        // Toggle fullscreen with F12 key
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
                        
                    case SDLK_E: // Toggle image enhancements
                        {
                            options->enhancement_enabled = !options->enhancement_enabled;
                            // Force reload of only the currently visible images to apply/remove enhancements
                            unload_images_for_view(get_current_view());
                            load_images_for_view(get_current_view());
                        }
                        break;
                        
                    case SDLK_H: // Show help
                        printf("\n=== Image Comic Viewer - Keyboard Controls ===\n");
                        printf("Arrow Keys / Space / Backspace : Navigate pages\n");
                        printf("Home / End                     : First / Last page\n");
                        printf("1 / 2                         : Single / Double page mode\n");
                        printf("F / F12                       : Toggle fullscreen\n");
                        printf("Z                             : Toggle zoom mode\n");
                        printf("+/- (or numpad)               : Zoom in/out\n");
                        printf("E                             : Toggle image enhancements\n");
                        printf("H                             : Show this help\n");
                        printf("Escape                        : Exit\n");
                        printf("==============================================\n\n");
                        break;
                }
                break;
                
            case SDL_EVENT_WINDOW_RESIZED:
                // Update window dimensions
                viewer.window_width = event.window.data1;
                viewer.window_height = event.window.data2;
                
                // Update logical presentation for HiDPI
                SDL_GetWindowSizeInPixels(viewer.window, &viewer.drawable_width, &viewer.drawable_height);
                
                if (viewer.drawable_width != viewer.window_width || viewer.drawable_height != viewer.window_height) {
                    SDL_SetRenderLogicalPresentation(viewer.renderer, 
                                                    viewer.window_width, 
                                                    viewer.window_height,
                                                    SDL_LOGICAL_PRESENTATION_LETTERBOX);
                }
                break;
                
            case SDL_EVENT_WINDOW_EXPOSED:
                // Window needs to be redrawn
                render_current_view();
                break;
        }
    }
}

static void render_current_view(void) {
    // Clear the screen with black background
    SDL_SetRenderDrawColor(viewer.renderer, 0, 0, 0, 255);
    SDL_RenderClear(viewer.renderer);

    if (viewer.page_turning_in_progress) {
        ImageEntry *current_img = &viewer.images[get_current_view()];
        ImageEntry *next_img = &viewer.images[viewer.target_view];

        // Ensure both textures are loaded
        if (!current_img->texture || !next_img->texture) {
            viewer.page_turning_in_progress = false; // Stop animation if textures are missing
            return;
        }

        // Calculate scaling to fit in the window while maintaining aspect ratio
        float scale_x = (float)viewer.drawable_width / current_img->width;
        float scale_y = (float)viewer.drawable_height / current_img->height;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;

        int scaled_width = (int)(current_img->width * scale);
        int scaled_height = (int)(current_img->height * scale);

        int x = (viewer.window_width - scaled_width) / 2;
        int y = (viewer.window_height - scaled_height) / 2;

        // Render the current page
        SDL_FRect current_rect = {(float)x, (float)y, (float)scaled_width, (float)scaled_height};
        SDL_RenderTexture(viewer.renderer, current_img->texture, &current_img->crop_rect, &current_rect);

        // Render the next page with a page turn effect
        SDL_FRect next_rect = {(float)x, (float)y, (float)scaled_width, (float)scaled_height};
        
        if (viewer.direction == 1) { // Forward page turn
            // In SDL3, we need to create a custom implementation for the page turn effect
            // using SDL_RenderGeometry or simpler methods
            
            // For now, implement a simple horizontal slide effect
            float slide_position = (float)scaled_width * (1.0f - viewer.page_turn_progress);
            next_rect.x = x + slide_position;
            
            // Render the next image with current progress
            SDL_RenderTexture(viewer.renderer, next_img->texture, NULL, &next_rect);
        } else { // Backward page turn
            // Similar implementation for backward page turn
            float slide_position = -((float)scaled_width * (1.0f - viewer.page_turn_progress));
            next_rect.x = x + slide_position;
            
            // Render the previous image with current progress
            SDL_RenderTexture(viewer.renderer, next_img->texture, NULL, &next_rect);
        }
        
        // Update the animation progress
        viewer.page_turn_progress += 0.05f; // Adjust speed as needed
        if (viewer.page_turn_progress >= 1.0f) {
            // Animation complete
            viewer.page_turning_in_progress = false;
            set_current_view(viewer.target_view);
        }
    } else {
        // Normal rendering
        ImageView *current_display_view = viewer.current_view_node;
        if (!current_display_view) return;
        
        int num_images_in_this_view = current_display_view->count;

        float display_area_width = (float)viewer.drawable_width;
        float display_area_height = (float)viewer.drawable_height;

        float overall_content_start_x = display_area_width; // Initialize to max
        float overall_content_end_x = 0.0f;                 // Initialize to min
        bool any_image_rendered = false;

        SDL_Color left_gradient_color, right_gradient_color;
        
        // Prepare for zoomed view calculations
        float scale_multiplier = 1.0f;
        float viewport_offset_x = 0.0f;
        float viewport_offset_y = 0.0f;
        
        // If zoomed, calculate the offset and scale
        if (viewer.zoomed) {
            scale_multiplier = viewer.zoom_level;


        }
        
        // Now do the actual rendering
        for (int i = 0; i < num_images_in_this_view; i++) {
            int image_idx = current_display_view->image_indices[i];

            ImageEntry *img = &viewer.images[image_idx];
            if (img->texture && img->width > 0 && img->height > 0) {
                any_image_rendered = true;
                
                // Calculate scaling - adjusted for zoom if needed
                float base_scale_y = display_area_height / img->height;
                float scale = base_scale_y;
                
                if (viewer.zoomed) {
                    scale = base_scale_y * scale_multiplier;
                }
                
                if (scale <= 1e-6f) scale = 1e-6f; // Prevent zero or negative scale

                int scaled_width = (int)(img->width * scale);
                int scaled_height = (int)(img->height * scale);
                if (scaled_width <= 0) scaled_width = 1; // Ensure positive dimensions
                if (scaled_height <= 0) scaled_height = 1;

                // Calculate positions
                float x_pos_render;
                float y_pos_render;
                
                // Normal (non-zoomed) positioning
                // First pass: calculate height-based scaling for all images
                if (i == 0) {
                    // First image is centered in the window
                    float total_width = 0;
                    for (int j = 0; j < num_images_in_this_view; j++) {
                        int idx = current_display_view->image_indices[j];
                        if (idx >= 0 && idx < viewer.image_count && 
                            viewer.images[idx].texture && 
                            viewer.images[idx].width > 0 && 
                            viewer.images[idx].height > 0) {
                            total_width += viewer.images[idx].width * base_scale_y;
                        }
                    }
                    // Center the entire group of images
                    x_pos_render = (display_area_width - total_width) / 2.0f;
                } else {
                    // Subsequent images are placed directly after the previous image
                    x_pos_render = overall_content_end_x;
                }
                
                // Center the image vertically in the window
                y_pos_render = (display_area_height - scaled_height) / 2.0f;

                // Update overall content extents
                if (x_pos_render < overall_content_start_x) {
                    overall_content_start_x = x_pos_render;
                }
                if (x_pos_render + scaled_width > overall_content_end_x) {
                    overall_content_end_x = x_pos_render + scaled_width;
                }

                if (i == 0) {
                    // Analyze the left edge of the first image
                    analyze_image_left_edge(image_idx, &left_gradient_color);
                }
                
                if (i == (num_images_in_this_view - 1)) {
                    // Analyze the right edge of the last image
                    analyze_image_right_edge(image_idx, &right_gradient_color);
                }

                SDL_FRect dest_rect = {x_pos_render, y_pos_render, (float)scaled_width, (float)scaled_height};
                SDL_RenderTexture(viewer.renderer, img->texture, &img->crop_rect, &dest_rect);
            }
        }

        if (any_image_rendered) {
            SDL_FRect left_rect_gradient = {0, 0, overall_content_start_x, display_area_height};
            if (left_rect_gradient.w > 0.5f) { // Use a small threshold for float comparison
                render_horizontal_gradient_hsl(viewer.renderer, left_rect_gradient, left_gradient_color, false);
            }

            SDL_FRect right_rect_gradient = {overall_content_end_x, 0,
                                     display_area_width - overall_content_end_x,
                                     display_area_height};
            if (right_rect_gradient.w > 0.5f) {
                render_horizontal_gradient_hsl(viewer.renderer, right_rect_gradient, right_gradient_color, true);
            }
        }
    }
    
    display_info();

    // Update screen
    SDL_RenderPresent(viewer.renderer);
}

void display_info()
{
    // Only show progress indicator if we have more than one image
    if (viewer.image_count <= 1) return;
       
    // Check if we should display the progress indicator
    Uint64 current_time = SDL_GetTicks();
    Uint64 elapsed_time = current_time - viewer.last_page_change_time;
    
    // Only show progress indicator for 2 seconds (2000 ms) after a page change
    if (elapsed_time <= 2000) {
        // Calculate progress as a value between 0.0 and 1.0
        int view_count = get_view_count();
        float progress = (float)get_current_view() / (float)(view_count - 1);
        
        // Circle properties
        int radius = 40;
        int centerX = 100;  // Moved further from left edge
        int centerY = 50;

        // Draw the progress indicator
        draw_progress_indicator(viewer.renderer, progress, centerX, centerY, radius); // Pass viewer.renderer
                
        // Display page number and total
        char info_text[64];
        snprintf(info_text, sizeof(info_text), "%d / %d %s", 
                get_current_view() + 1, view_count,
                options->enhancement_enabled ? "[E+]" : "[E-]");

        SDL_Texture *text_texture = render_text(info_text, white);
        if (text_texture) {
            float text_width, text_height;
            SDL_GetTextureSize(text_texture, &text_width, &text_height);
            
            // Ensure text doesn't get cut off on the left edge
            float text_x = (float)(centerX - text_width / 2);
            if (text_x < 10) {  // Minimum 10px margin from left edge
                text_x = 10;
            }
            
            SDL_FRect text_rect = {
                text_x,
                (float)(centerY + radius + 10),
                (float)text_width,
                (float)text_height
            };
            
            SDL_RenderTexture(viewer.renderer, text_texture, NULL, &text_rect);
            SDL_DestroyTexture(text_texture);
        }
    }
}

static void free_resources(void) {
    // Free image resources
    for (int i = 0; i < viewer.image_count; i++) {
        if (viewer.images[i].surface) {
            SDL_DestroySurface(viewer.images[i].surface);
            viewer.images[i].surface = NULL;
        }
        if (viewer.images[i].texture) {
            SDL_DestroyTexture(viewer.images[i].texture);
            viewer.images[i].texture = NULL;
        }
        free(viewer.images[i].path);
        viewer.images[i].path = NULL;
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

// Function to get the most prominent color from a surface region
static SDL_Color get_dominant_color(SDL_Surface *surface, int x, int y, int width, int height) {
    // Default color (black)
    SDL_Color dominant = {0, 0, 0, 255};
    
    // Use a different approach to avoid the large array allocation
    // We'll use color buckets with fewer bits per channel
    #define COLOR_BITS 5
    #define COLOR_BUCKETS (1 << COLOR_BITS)
    #define COLOR_MASK ((1 << COLOR_BITS) - 1)
    
    // Allocate the frequency counter on the heap instead of stack
    unsigned int *color_freq = calloc(COLOR_BUCKETS * COLOR_BUCKETS * COLOR_BUCKETS, sizeof(unsigned int));
    if (!color_freq) return dominant;
    
    unsigned int max_freq = 0;
    int dominant_index = 0;
    
    // Get pixel format
    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(surface->format);
    int bpp = fmt->bytes_per_pixel;
    
    // Scan the specified region with bounds checking
    int sample_step = 2; // Sample every 2nd pixel to speed up analysis
    
    uint8_t *pixels = (uint8_t *)surface->pixels;
    int pitch = surface->pitch;
    
    for (int j = y; j < y + height; j += sample_step) {
        for (int i = x; i < x + width; i += sample_step) {
            // Skip if outside bounds
            if (i < 0 || i >= surface->w || j < 0 || j >= surface->h) continue;
            
            // Extract the pixel
            uint8_t *p = pixels + j * pitch + i * bpp;
            uint32_t pixel = 0;
            
            // Be very careful with pixel access
            switch (bpp) {
                case 1: pixel = *p; break;
                case 2: pixel = *(uint16_t *)p; break;
                case 3: 
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                    #else
                        pixel = p[0] | p[1] << 8 | p[2] << 16; 
                    #endif
                    break;
                case 4: pixel = *(uint32_t *)p; break;
                default: continue; // Skip unknown formats
            }
            
            // Convert to RGB
            uint8_t r, g, b, a;
            SDL_GetRGBA(pixel, fmt, SDL_GetSurfacePalette(surface), &r, &g, &b, &a);
            
            // Skip almost black or almost white pixels
            if ((r < 15 && g < 15 && b < 15) || (r > 240 && g > 240 && b > 240)) {
                continue;
            }
            
            // Reduce color depth to fit in our buckets
            r >>= (8 - COLOR_BITS);
            g >>= (8 - COLOR_BITS);
            b >>= (8 - COLOR_BITS);
            
            // Calculate bucket index
            int bucket_index = (r * COLOR_BUCKETS * COLOR_BUCKETS) + (g * COLOR_BUCKETS) + b;
            
            // Increment frequency
            color_freq[bucket_index]++;
            if (color_freq[bucket_index] > max_freq) {
                max_freq = color_freq[bucket_index];
                dominant_index = bucket_index;
            }
        }
    }
    
    // Convert bucket index back to RGB
    if (max_freq > 0) {
        int r = (dominant_index / (COLOR_BUCKETS * COLOR_BUCKETS)) & COLOR_MASK;
        int g = (dominant_index / COLOR_BUCKETS) & COLOR_MASK;
        int b = dominant_index & COLOR_MASK;
        
        // Convert back to 8-bit channels
        dominant.r = (r << (8 - COLOR_BITS)) | (r >> (2 * COLOR_BITS - 8));
        dominant.g = (g << (8 - COLOR_BITS)) | (g >> (2 * COLOR_BITS - 8));
        dominant.b = (b << (8 - COLOR_BITS)) | (b >> (2 * COLOR_BITS - 8));
    }
    
    free(color_freq);
    return dominant;
}

// This function analyzes the image and extracts the dominant color from the left edge
static void analyze_image_left_edge(int index, SDL_Color *left_color) {
    // Default to black if something goes wrong
    *left_color = (SDL_Color){0, 0, 0, 255};
    
    // Basic validation
    if (index < 0 || index >= viewer.image_count || !viewer.images[index].path) return;
    
    SDL_Surface *surface = viewer.images[index].surface;
    if (!surface) {
        // Attempt to load surface if not already loaded (e.g., if only texture was created directly)
        // This might happen if create_high_quality_texture was called but surface was freed or not kept.
        // For robust edge analysis, the surface is needed.
        // However, typically, create_high_quality_texture should keep the surface if analysis is needed.
        // If it's critical and surface is often NULL here, load_image might need to ensure surface is populated.
        // For now, assume surface should be available if image is loaded.
        fprintf(stderr, "Failed to get surface for left edge analysis (image %d): Surface is NULL.\n", index);
        // We could try IMG_Load here again if viewer.images[index].path is valid,
        // but that implies a design issue if it's not kept.
        return;
    }
    
    // Get image dimensions
    int width = surface->w;
    int height = surface->h;
    
    // Sample pixels from the left edge (8% of width)
    int edge_width = width * 0.08;
    if (edge_width < 1) edge_width = 1;
    if (edge_width > width) edge_width = width; // Cap at image width

    // Get dominant color from left edge
    *left_color = get_dominant_color(surface, 0, 0, edge_width, height);
}

// This function analyzes the image and extracts the dominant color from the right edge
static void analyze_image_right_edge(int index, SDL_Color *right_color) {
    // Default to black if something goes wrong
    *right_color = (SDL_Color){0, 0, 0, 255};

    // Basic validation
    if (index < 0 || index >= viewer.image_count || !viewer.images[index].path) return;

    SDL_Surface *surface = viewer.images[index].surface;
    if (!surface) {
        fprintf(stderr, "Failed to get surface for right edge analysis (image %d): Surface is NULL.\n", index);
        return;
    }

    // Get image dimensions
    int width = surface->w;
    int height = surface->h;

    // Sample pixels from the right edge (8% of width)
    int edge_width = width * 0.08;
    if (edge_width < 1) edge_width = 1;
    if (edge_width > width) edge_width = width; // Cap at image width
    
    // Get dominant color from right edge
    *right_color = get_dominant_color(surface, width - edge_width, 0, edge_width, height);
}

// Function to render text as a texture
static SDL_Texture* render_text(const char *text, SDL_Color color) {
    if (!viewer.font || !text) return NULL;

    SDL_Surface *surface = TTF_RenderText_Blended(viewer.font, text, 0, color);
    if (!surface) {
        fprintf(stderr, "Failed to render text: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(viewer.renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture) {
        fprintf(stderr, "Failed to create texture from text: %s\n", SDL_GetError());
    }

    return texture;
}

// Function to select monitor and get its position
static bool select_monitor(int monitor_index, int *x, int *y) {
    int num_displays;
    SDL_DisplayID* display_id = SDL_GetDisplays(&num_displays);
    if (num_displays <= 0) {
        fprintf(stderr, "No video displays available: %s\n", SDL_GetError());
        return false;
    }
    SDL_free(display_id);

    if (monitor_index < 0 || monitor_index >= num_displays) {
        fprintf(stderr, "Invalid monitor index: %d\n", monitor_index);
        return false;
    }

    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(monitor_index, &bounds) != 0) {
        fprintf(stderr, "Failed to get display bounds for monitor %d: %s\n", monitor_index, SDL_GetError());
        return false;
    }

    *x = bounds.x;
    *y = bounds.y;
    return true;
}

// Helper function for high-quality image scaling with border detection and removal
static void create_texture(SDL_Renderer *renderer, ImageEntry *image) {
    // Load the image as a surface using FreeImage
    image->surface = image_load_surface(image->path, options);
    if (!image->surface) {
        fprintf(stderr, "Failed to load image %s with FreeImage\n", image->path);
        return;
    }

    // Detect and crop white borders
    int left = 0, right = image->surface->w - 1;
    int top = 0, bottom = image->surface->h - 1;
    int threshold = 240; // Threshold for considering a pixel "white" (0-255)
    int required_non_white = 3; // Number of non-white pixels required to stop scanning
    
    // Analyze pixels to detect borders
    uint8_t *pixels = (uint8_t*)image->surface->pixels;
    int pitch = image->surface->pitch;
    const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(image->surface->format);
    int bpp = details->bytes_per_pixel;

    SDL_Palette *palette = SDL_GetSurfacePalette(image->surface);
    
    // Scan from left edge inward
    for (left = 0; left < image->surface->w / 2; left++) {
        int non_white_count = 0;
        
        for (int y = 0; y < image->surface->h; y += 2) { // Sample every other pixel for speed
            uint32_t pixel = 0;
            uint8_t *p = pixels + y * pitch + left * bpp;
            
            switch (bpp) {
                case 1: pixel = *p; break;
                case 2: pixel = *(uint16_t*)p; break;
                case 3: 
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                    #else
                        pixel = p[0] | p[1] << 8 | p[2] << 16; 
                    #endif
                    break;
                case 4: pixel = *(uint32_t*)p; break;
            }
            
            uint8_t r, g, b, a;
            SDL_GetRGBA(pixel, details, palette, &r, &g, &b, &a);
            
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
    
    // Scan from right edge inward
    for (right = image->surface->w - 1; right > left + 100; right--) { // Ensure min width
        int non_white_count = 0;
        
        for (int y = 0; y < image->surface->h; y += 2) {
            uint32_t pixel = 0;
            uint8_t *p = pixels + y * pitch + right * bpp;
            
            switch (bpp) {
                case 1: pixel = *p; break;
                case 2: pixel = *(uint16_t*)p; break;
                case 3: 
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                    #else
                        pixel = p[0] | p[1] << 8 | p[2] << 16; 
                    #endif
                    break;
                case 4: pixel = *(uint32_t*)p; break;
            }
            
            uint8_t r, g, b, a;
            SDL_GetRGBA(pixel, SDL_GetPixelFormatDetails(image->surface->format), palette, &r, &g, &b, &a);
            
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
    
    // Scan from top edge down
    for (top = 0; top < image->surface->h / 2; top++) {
        int non_white_count = 0;
        
        for (int x = left; x <= right; x += 2) {
            uint32_t pixel = 0;
            uint8_t *p = pixels + top * pitch + x * bpp;
            
            switch (bpp) {
                case 1: pixel = *p; break;
                case 2: pixel = *(uint16_t*)p; break;
                case 3: 
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                    #else
                        pixel = p[0] | p[1] << 8 | p[2] << 16; 
                    #endif
                    break;
                case 4: pixel = *(uint32_t*)p; break;
            }
            
            uint8_t r, g, b, a;
            SDL_GetRGBA(pixel, SDL_GetPixelFormatDetails(image->surface->format), palette, &r, &g, &b, &a);
            
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
    
    // Scan from bottom edge up
    for (bottom = image->surface->h - 1; bottom > top + 100; bottom--) { // Ensure min height
        int non_white_count = 0;
        
        for (int x = left; x <= right; x += 2) {
            uint32_t pixel = 0;
            uint8_t *p = pixels + bottom * pitch + x * bpp;
            
            switch (bpp) {
                case 1: pixel = *p; break;
                case 2: pixel = *(uint16_t*)p; break;
                case 3: 
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                    #else
                        pixel = p[0] | p[1] << 8 | p[2] << 16; 
                    #endif
                        break;
                case 4: pixel = *(uint32_t*)p; break;
            }
            
            uint8_t r, g, b, a;
            SDL_GetRGBA(pixel, SDL_GetPixelFormatDetails(image->surface->format), palette, &r, &g, &b, &a);
            
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

    SDL_FRect crop_rect = {left, top, right - left + 1, bottom - top + 1};
    if (crop_rect.w <= 0 || crop_rect.h <= 0) {
        // reset crop rect to full image size
        crop_rect.x = 0;
        crop_rect.y = 0;
        crop_rect.w = image->surface->w;
        crop_rect.h = image->surface->h;
    }
    image->crop_rect = crop_rect;

    // Create a texture from the surface
    image->texture = SDL_CreateTextureFromSurface(renderer, image->surface);
    if (!image->texture) {
        fprintf(stderr, "Failed to create texture: %s\n", SDL_GetError());
    }
}

// Helper function for progress callback
static void update_progress(float progress, const char *message) {
    progress_bar_update(progress, message);
}

void view_changed(ImageView *old_view_node, ImageView *new_view_node) {
    // Update the page change timer
    viewer.last_page_change_time = SDL_GetTicks();
    viewer.show_progress_indicator = true;
    
    // Unload old view images
    if (old_view_node) {
        for (int i = 0; i < old_view_node->count; i++) {
            int img_idx = old_view_node->image_indices[i];
            unload_image(img_idx);
        }
    }

    // Load new view images
    if (new_view_node) {
        for (int i = 0; i < new_view_node->count; i++) {
            int img_idx = new_view_node->image_indices[i];
            load_image(img_idx);
        }
    }
}

void previous_view() {
    if (!viewer.current_view_node || !viewer.current_view_node->prev || viewer.page_turning_in_progress) {
        return;
    }

    ImageView *old_view_node = viewer.current_view_node;
    viewer.current_view_node = viewer.current_view_node->prev;
    viewer.current_view_index--;
    view_changed(old_view_node, viewer.current_view_node);
}


void next_view() {
    if (!viewer.current_view_node || !viewer.current_view_node->next || viewer.page_turning_in_progress) {
        return;
    }

    ImageView *old_view_node = viewer.current_view_node;
    viewer.current_view_node = viewer.current_view_node->next;
    viewer.current_view_index++;
    view_changed(old_view_node, viewer.current_view_node);
}

static void generate_default_views() {
    // Clear any existing views
    free_all_views();
    
    viewer.view_count = 0;
    int i = 0;
    
    ImageView *prev_view = NULL;
    while (i < viewer.image_count) {
        ImageView *view = create_view_node(prev_view);
        if (!view) {
            fprintf(stderr, "Failed to create view node\n");
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
static ImageView* create_view_node(ImageView* prev_view) {
    ImageView *view = malloc(sizeof(ImageView));
    if (!view) {
        fprintf(stderr, "Failed to allocate memory for view node\n");
        return NULL;
    }
    
    // Initialize the view
    view->count = 1; // Default to one image per view
    view->total_width = 0;
    view->max_height = 0;
    view->crop_rect = (SDL_FRect){0, 0, 0, 0};
    view->next = NULL;
    view->prev = prev_view;

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

static ImageView* get_view_by_index(int index) {
    if (index < 0) return NULL;
    
    ImageView *current = viewer.first_view;
    for (int i = 0; i < index && current; i++) {
        current = current->next;
    }
    return current;
}

static int get_current_view_index(void) {
    if (!viewer.current_view_node || !viewer.first_view) return 0;
    
    ImageView *current = viewer.first_view;
    int index = 0;
    while (current && current != viewer.current_view_node) {
        current = current->next;
        index++;
    }
    return index;
}