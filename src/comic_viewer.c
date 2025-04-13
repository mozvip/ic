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
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "comic_viewer.h"
#include "comic_loaders.h"
#include "progress_bar.h"

// Maximum number of images we can handle
#define MAX_IMAGES 1000

// Global state
static struct {
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
} viewer = {0};

// Forward declarations for internal functions
static void free_resources(void);
static void handle_events(void);
static void previous_page();
static void next_page();
static void render_current_image(void);
static void display_info();
static bool load_image(int index);
static void unload_image(int index);
static void toggle_fullscreen(void);
static SDL_Color get_dominant_color(SDL_Surface *surface, int x, int y, int width, int height);
static void analyze_image_edges(int index, SDL_Color *left_color, SDL_Color *right_color);
static SDL_Texture* render_text(const char *text, SDL_Color color);
static bool select_monitor(int monitor_index, int *x, int *y);
static void create_high_quality_texture(SDL_Renderer *renderer, ImageEntry *image);
static void update_progress(float progress, const char *message);

bool comic_viewer_init(int monitor_index) {
    // Force SDL to use Wayland backend
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland");
    
    // Enable HiDPI scaling
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    
    // Set best quality for scaling operations
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");  // 0=nearest, 1=linear, 2=anisotropic
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    // Initialize SDL_image
    int img_flags = IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP;
    if (!(IMG_Init(img_flags) & img_flags)) {
        fprintf(stderr, "SDL_image could not initialize! SDL_image Error: %s\n", IMG_GetError());
        SDL_Quit();
        return false;
    }
    
    // Initialize SDL_ttf
    if (TTF_Init() < 0) {
        fprintf(stderr, "SDL_ttf could not initialize! SDL_ttf Error: %s\n", TTF_GetError());
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    // Determine monitor position
    int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    if (!select_monitor(monitor_index, &x, &y)) {
        fprintf(stderr, "Failed to select monitor %d\n", monitor_index);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    // Create window
    viewer.window_width = 1024;
    viewer.window_height = 768;
    viewer.monitor_index = monitor_index;
    viewer.window = SDL_CreateWindow("IC - Image Comic Viewer",
                                    x, y,
                                    viewer.window_width, viewer.window_height,
                                    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (viewer.window == NULL) {
        fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    // Create renderer with enhanced quality settings
    viewer.renderer = SDL_CreateRenderer(viewer.window, -1, 
                                        SDL_RENDERER_ACCELERATED | 
                                        SDL_RENDERER_PRESENTVSYNC);
    if (viewer.renderer == NULL) {
        fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(viewer.window);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }
    
    // Initialize the progress bar
    if (!progress_bar_init(viewer.renderer)) {
        fprintf(stderr, "Warning: Failed to initialize progress bar. Loading will proceed without visual feedback.\n");
        // Continue without progress bar - non-fatal
    }
    
    // Get the actual window size and drawable size for HiDPI scaling
    int display_w, display_h;
    SDL_GetWindowSize(viewer.window, &viewer.window_width, &viewer.window_height);
    
    // Set logical size to handle HiDPI scaling automatically
    if (display_w > viewer.window_width || display_h > viewer.window_height) {
        SDL_RenderSetLogicalSize(viewer.renderer, viewer.window_width, viewer.window_height);
        printf("HiDPI detected: Window size: %dx%d, Drawable size: %dx%d\n", 
               viewer.window_width, viewer.window_height, display_w, display_h);
    }

    SDL_GL_GetDrawableSize(viewer.window, &viewer.drawable_width, &viewer.drawable_height);

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
        fprintf(stderr, "Warning: Failed to load font: %s\n", TTF_GetError());
        // Continue without a font - we'll handle the null case when rendering
    }

    // Initialize viewer state
    viewer.type = SOURCE_UNKNOWN;
    viewer.source_path = NULL;
    viewer.image_count = 0;
    viewer.current_image = 0;
    viewer.preloaded_image = -1;
    viewer.running = false;
    viewer.fullscreen = false;
    viewer.archive = NULL;

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

    // Determine source type
    if (S_ISDIR(path_stat.st_mode)) {
        viewer.type = SOURCE_DIRECTORY;
        return load_directory(path, viewer.images, &viewer.image_count, MAX_IMAGES, update_progress);
    } else {
        // Check file extension to determine type
        size_t len = strlen(path);
        if (len > 4) {
            const char *ext = path + len - 4;
            if (strcasecmp(ext, ".cbz") == 0 || strcasecmp(ext, ".zip") == 0) {
                viewer.type = SOURCE_CBZ;
                viewer.archive = archive_open(path, ARCHIVE_TYPE_CBZ, &viewer.image_count, update_progress);
                return viewer.archive != NULL;
            } else if (strcasecmp(ext, ".cbr") == 0 || strcasecmp(ext, ".rar") == 0) {
                viewer.type = SOURCE_CBR;
                viewer.archive = archive_open(path, ARCHIVE_TYPE_CBR, &viewer.image_count, update_progress);
                return viewer.archive != NULL;
            } else if (strcasecmp(ext, ".pdf") == 0) {
                viewer.type = SOURCE_PDF;
                viewer.archive = archive_open(path, ARCHIVE_TYPE_PDF, &viewer.image_count, update_progress);
                return viewer.archive != NULL;
            }
        }
    }

    update_progress(1.0f, "Unsupported file type");
    fprintf(stderr, "Unsupported file type: %s\n", path);
    return false;
}

void comic_viewer_run(void) {
    if (viewer.image_count == 0) {
        fprintf(stderr, "No images to display\n");
        return;
    }

    // Load the first image
    if (!load_image(viewer.current_image)) {
        fprintf(stderr, "Failed to load first image\n");
        return;
    }
    
    // Preload the next image if available
    if (viewer.image_count > 1) {
        load_image(viewer.current_image + 1);
        viewer.preloaded_image = viewer.current_image + 1;
    }

    viewer.running = true;

    // Main loop
    while (viewer.running) {
        // Handle events
        handle_events();

        // Render the current image
        render_current_image();

        // Delay to reduce CPU usage
        SDL_Delay(10);
    }
}

void comic_viewer_cleanup(void) {
    // Clean up progress bar resources
    progress_bar_cleanup();
    
    // Close archive if open
    if (viewer.archive) {
        archive_close(viewer.archive);
        viewer.archive = NULL;
    }
    
    free_resources();
    IMG_Quit();
    SDL_Quit();
}

// Internal helper functions

static bool load_image(int index) {
    if (index < 0 || index >= viewer.image_count) return false;
    
    // If the texture is already loaded, do nothing
    if (viewer.images[index].texture != NULL) return true;
    
    if (viewer.archive) {
        // On-demand loading mode
        char *image_path = NULL;
        if (archive_get_image(viewer.archive, index, &image_path)) {
            // Store the path in our image entry
            viewer.images[index].path = image_path;
            
            // Load the image using our high-quality scaling function
            create_high_quality_texture(viewer.renderer, &viewer.images[index]);
            if (!viewer.images[index].texture) {
                fprintf(stderr, "Failed to load image %s: %s\n", image_path, IMG_GetError());
                return false;
            }
            
            // Store original dimensions
            SDL_QueryTexture(viewer.images[index].texture, NULL, NULL, 
                            &viewer.images[index].width, &viewer.images[index].height);
            
            return true;
        }
        return false;
    } else {
        // Standard loading mode
        create_high_quality_texture(viewer.renderer, &viewer.images[index]);
        if (!viewer.images[index].texture) {
            fprintf(stderr, "Failed to load image %s: %s\n", 
                    viewer.images[index].path, IMG_GetError());
            return false;
        }
        
        // Store original dimensions
        SDL_QueryTexture(viewer.images[index].texture, NULL, NULL, 
                        &viewer.images[index].width, &viewer.images[index].height);
        
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
            case SDL_QUIT:
                viewer.running = false;
                break;
                
            case SDL_MOUSEWHEEL:
                // Mouse wheel for page navigation
                if (event.wheel.y > 0) {  // Scroll up
                    previous_page();
                } else if (event.wheel.y < 0) {  // Scroll down
                    next_page();
                }
                break;
                
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        viewer.running = false;
                        break;
                        
                    case SDLK_RIGHT:
                    case SDLK_SPACE:
                    case SDLK_DOWN:
                        next_page();
                        break;
                        
                    case SDLK_LEFT:
                    case SDLK_UP:
                    case SDLK_BACKSPACE:
                        previous_page();
                        break;
                        
                    case SDLK_HOME:
                        // First image
                        if (viewer.current_image != 0) {
                            // Clean up any loaded textures except the first one
                            for (int i = 1; i < viewer.image_count; i++) {
                                unload_image(i);
                            }
                            viewer.current_image = 0;
                            load_image(0);
                            
                            // Preload the next image
                            if (viewer.image_count > 1) {
                                load_image(1);
                                viewer.preloaded_image = 1;
                            }
                        }
                        break;
                        
                    case SDLK_END:
                        // Last image
                        if (viewer.current_image != viewer.image_count - 1) {
                            // Clean up any loaded textures except the last one
                            for (int i = 0; i < viewer.image_count - 1; i++) {
                                unload_image(i);
                            }
                            viewer.current_image = viewer.image_count - 1;
                            load_image(viewer.current_image);
                            
                            // Preload the previous image
                            if (viewer.image_count > 1) {
                                load_image(viewer.image_count - 2);
                                viewer.preloaded_image = viewer.image_count - 2;
                            }
                        }
                        break;

                    case SDLK_f:
                        // Toggle fullscreen
                        toggle_fullscreen();
                        break;
                        
                    case SDLK_F12:
                        // Toggle fullscreen with F12 key
                        toggle_fullscreen();
                        break;
                }
                break;
                
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED || 
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    // Update window dimensions
                    viewer.window_width = event.window.data1;
                    viewer.window_height = event.window.data2;
                    
                    SDL_RenderSetLogicalSize(viewer.renderer, viewer.window_width, viewer.window_height);
                } else if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    // Window needs to be redrawn
                    render_current_image();
                }
                break;
        }
    }
}

void next_page()
{
    if (viewer.current_image == viewer.image_count - 1 || viewer.page_turning_in_progress) {
        return;
    }
    if (viewer.current_image > 0)
    {
        unload_image(viewer.current_image - 1); // Unload previous image to save memory
    }

    if (viewer.page_turning_enabled){
        viewer.target_image = viewer.current_image + 1;
        viewer.page_turning_enabled = true;
        viewer.page_turn_progress = 0.0f;
        viewer.direction = 1; // Set direction for page turn animation
    
        // Note: No need to explicitly load the image here as it should already be preloaded
    
        // Start preloading the next image if available
        if (viewer.current_image < viewer.image_count - 1)
        {
            load_image(viewer.current_image + 1);
            viewer.preloaded_image = viewer.current_image + 1;
        }
    } else {
        viewer.current_image++;
        load_image(viewer.current_image);
    }

}

void previous_page()
{
    if (viewer.current_image == 0  || viewer.page_turning_in_progress) {
        return;
    }
    if (viewer.current_image < viewer.image_count - 1)
    {
        unload_image(viewer.current_image + 1);
    }

    if (viewer.page_turning_enabled){
        viewer.target_image = viewer.current_image - 1;
        viewer.page_turning_enabled = true;
        viewer.page_turn_progress = 0.0f;
        viewer.direction = -1; // Set direction for page turn animation

        // Preload the previous image for faster backward navigation
        if (viewer.current_image > 0)
        {
            load_image(viewer.current_image - 1);
            viewer.preloaded_image = viewer.current_image - 1;
        }
    } else {
        viewer.current_image--;
        load_image(viewer.current_image);
    }
}

static void render_current_image(void) {
    // Clear the screen with black background
    SDL_SetRenderDrawColor(viewer.renderer, 0, 0, 0, 255);
    SDL_RenderClear(viewer.renderer);

    if (viewer.page_turning_in_progress) {
        ImageEntry *current_img = &viewer.images[viewer.current_image];
        ImageEntry *next_img = &viewer.images[viewer.target_image];

        // Ensure both textures are loaded
        if (!current_img->texture || !next_img->texture) {
            viewer.page_turning_enabled = false; // Stop animation if textures are missing
            return;
        }

        // Calculate scaling to fit in the window while maintaining aspect ratio

        float scale_x = viewer.drawable_width / current_img->width;
        float scale_y = viewer.drawable_height / current_img->height;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;

        int scaled_width = (int)(current_img->width * scale);
        int scaled_height = (int)(current_img->height * scale);

        int x = (viewer.window_width - scaled_width) / 2;
        int y = (viewer.window_height - scaled_height) / 2;

        // Render the current page
        SDL_Rect current_rect = {x, y, scaled_width, scaled_height};
        SDL_RenderCopy(viewer.renderer, current_img->texture, NULL, &current_rect);

        // Render the next page with a clipping effect
        SDL_Rect next_rect = {x, y, scaled_width, scaled_height};
        if (viewer.direction == 1) {
            // TODO
        } else {
            // TODO
        }
        int clip_width = (int)(scaled_width * viewer.page_turn_progress);
        SDL_Rect clip_rect = {0, 0, clip_width, scaled_height};
        SDL_RenderSetClipRect(viewer.renderer, &clip_rect);
        SDL_RenderCopy(viewer.renderer, next_img->texture, NULL, &next_rect);
        SDL_RenderSetClipRect(viewer.renderer, NULL);

        // Update the animation progress
        viewer.page_turn_progress += 0.05f; // Adjust speed as needed
        if (viewer.page_turn_progress >= 1.0f) {
            // Animation complete
            viewer.page_turning_enabled = false;
            viewer.current_image = viewer.target_image;
        }
    } else {
        // Normal rendering
        // Render the current image
        ImageEntry *img = &viewer.images[viewer.current_image];
        if (img->texture) {
            // Calculate scaling to fit in the window while maintaining aspect ratio
            float scale_x = (float)viewer.window_width / img->width;
            float scale_y = (float)viewer.window_height / img->height;
            float scale = (scale_x < scale_y) ? scale_x : scale_y;
            
            // For best quality, calculate dimensions with minimal rounding errors
            int scaled_width = (int)(img->width * scale);
            int scaled_height = (int)(img->height * scale);
            
            // Center the image
            int x = (viewer.window_width - scaled_width) / 2;
            int y = (viewer.window_height - scaled_height) / 2;
            
            SDL_Color left_color, right_color;
            analyze_image_edges(viewer.current_image, &left_color, &right_color);
            
            // Draw left side background with the dominant color from the left edge
            SDL_SetRenderDrawColor(viewer.renderer, left_color.r, left_color.g, left_color.b, 255);
            SDL_Rect left_rect = {0, 0, x, viewer.window_height};
            SDL_RenderFillRect(viewer.renderer, &left_rect);
            
            // Draw right side background with the dominant color from the right edge
            SDL_SetRenderDrawColor(viewer.renderer, right_color.r, right_color.g, right_color.b, 255);
            SDL_Rect right_rect = {x + scaled_width, 0, viewer.window_width - x - scaled_width, viewer.window_height};
            SDL_RenderFillRect(viewer.renderer, &right_rect);
            
            // Draw the image with high-quality rendering
            SDL_Rect dest_rect = {x, y, scaled_width, scaled_height};
            
            // Use high-quality rendering with a specific blend mode for better results
            SDL_SetTextureBlendMode(img->texture, SDL_BLENDMODE_NONE);
            SDL_RenderCopy(viewer.renderer, img->texture, NULL, &dest_rect);
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
    
    // Calculate progress as a value between 0.0 and 1.0
    float progress = (float)viewer.current_image / (float)(viewer.image_count - 1);
    
    // Circle properties
    int radius = 40;
    int centerX = 50;
    int centerY = 50;
    
    // Color constants
    SDL_Color bgColor = {50, 50, 50, 180};    // Semi-transparent dark gray
    SDL_Color fgColor = {255, 255, 255, 255}; // White
    
    // Draw semi-transparent background circle
    SDL_SetRenderDrawBlendMode(viewer.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(viewer.renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
    
    // Draw a filled circle using triangles like a pie chart
    int segments = 36; // Number of segments for a full circle
    float angle_step = 2.0f * M_PI / segments;
    
    // Background circle
    for (int i = 0; i < segments; i++) {
        float angle1 = i * angle_step;
        float angle2 = (i + 1) * angle_step;
        
        int x1 = centerX + (int)(cos(angle1) * radius);
        int y1 = centerY + (int)(sin(angle1) * radius);
        int x2 = centerX + (int)(cos(angle2) * radius);
        int y2 = centerY + (int)(sin(angle2) * radius);
        
        // Draw a triangle from center to two points on the circle
        SDL_Vertex vertices[3] = {
            {{(float)centerX, (float)centerY}, {bgColor.r, bgColor.g, bgColor.b, bgColor.a}, {0, 0}},
            {{(float)x1, (float)y1}, {bgColor.r, bgColor.g, bgColor.b, bgColor.a}, {0, 0}},
            {{(float)x2, (float)y2}, {bgColor.r, bgColor.g, bgColor.b, bgColor.a}, {0, 0}}
        };
        
        SDL_RenderGeometry(viewer.renderer, NULL, vertices, 3, NULL, 0);
    }
    
    // Draw progress fill
    SDL_SetRenderDrawColor(viewer.renderer, fgColor.r, fgColor.g, fgColor.b, fgColor.a);
    
    // Calculate filled segments based on progress
    int filledSegments = (int)(progress * segments);
    if (filledSegments < 1 && viewer.current_image > 0) filledSegments = 1;
    
    // Starting angle: -90 degrees (top of the circle)
    float startAngle = -M_PI / 2.0f;
    
    for (int i = 0; i < filledSegments; i++) {
        float angle1 = startAngle + (i * angle_step);
        float angle2 = startAngle + ((i + 1) * angle_step);
        
        int x1 = centerX + (int)(cos(angle1) * radius);
        int y1 = centerY + (int)(sin(angle1) * radius);
        int x2 = centerX + (int)(cos(angle2) * radius);
        int y2 = centerY + (int)(sin(angle2) * radius);
        
        // Draw a triangle from center to two points on the circle
        SDL_Vertex vertices[3] = {
            {{(float)centerX, (float)centerY}, {fgColor.r, fgColor.g, fgColor.b, fgColor.a}, {0, 0}},
            {{(float)x1, (float)y1}, {fgColor.r, fgColor.g, fgColor.b, fgColor.a}, {0, 0}},
            {{(float)x2, (float)y2}, {fgColor.r, fgColor.g, fgColor.b, fgColor.a}, {0, 0}}
        };
        
        SDL_RenderGeometry(viewer.renderer, NULL, vertices, 3, NULL, 0);
    }
    
    // Reset blend mode
    SDL_SetRenderDrawBlendMode(viewer.renderer, SDL_BLENDMODE_NONE);
}

static void free_resources(void) {
    // Free image resources
    for (int i = 0; i < viewer.image_count; i++) {
        if (viewer.images[i].surface) {
            SDL_FreeSurface(viewer.images[i].surface);
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
        // Use borderless fullscreen for better Wayland compatibility
        SDL_SetWindowFullscreen(viewer.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(viewer.window, 0);
        
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
    
    SDL_GL_GetDrawableSize(viewer.window, &viewer.drawable_width, &viewer.drawable_height);
    SDL_GetWindowSize(viewer.window, &viewer.window_width, &viewer.window_height);

    // Re-apply the logical size after toggling fullscreen to maintain HiDPI settings
    if (viewer.renderer) {       
        // Only set logical size if there's a difference (HiDPI)
        if (viewer.drawable_width != viewer.window_width || viewer.drawable_height != viewer.window_height) {
            SDL_RenderSetLogicalSize(viewer.renderer, viewer.window_width, viewer.window_height);
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
    SDL_PixelFormat *fmt = surface->format;
    int bpp = surface->format->BytesPerPixel;
    
    // Scan the specified region with bounds checking
    int sample_step = 2; // Sample every 2nd pixel to speed up analysis
    
    for (int j = y; j < y + height; j += sample_step) {
        for (int i = x; i < x + width; i += sample_step) {
            // Skip if outside bounds
            if (i < 0 || i >= surface->w || j < 0 || j >= surface->h) continue;
            
            // Extract the pixel
            Uint8 *p = (Uint8 *)surface->pixels + j * surface->pitch + i * bpp;
            Uint32 pixel = 0;
            
            // Be very careful with pixel access
            switch (bpp) {
                case 1: pixel = *p; break;
                case 2: pixel = *(Uint16 *)p; break;
                case 3: 
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                    #else
                        pixel = p[0] | p[1] << 8 | p[2] << 16; 
                    #endif
                    break;
                case 4: pixel = *(Uint32 *)p; break;
                default: continue; // Skip unknown formats
            }
            
            // Convert to RGB
            Uint8 r, g, b;
            SDL_GetRGB(pixel, fmt, &r, &g, &b);
            
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

// This function analyzes the image and extracts the dominant colors from the edges
static void analyze_image_edges(int index, SDL_Color *left_color, SDL_Color *right_color) {
    // Default to black if something goes wrong
    *left_color = (SDL_Color){0, 0, 0, 255};
    *right_color = (SDL_Color){0, 0, 0, 255};
    
    // Basic validation
    if (index < 0 || index >= viewer.image_count || !viewer.images[index].path) return;
    
    // Load the image as a surface for analysis, we need to do this even if texture is loaded
    // since we can't easily access pixel data from textures
    SDL_Surface *surface = viewer.images[index].surface;
    if (!surface) {
        fprintf(stderr, "Failed to load image for analysis: %s\n", IMG_GetError());
        return;
    }
    
    // Get image dimensions
    int width = surface->w;
    int height = surface->h;
    
    // Sample pixels from the left edge (8% of width)
    int edge_width = width * 0.08;
    if (edge_width < 1) edge_width = 1;

    // Lock surface for pixel access
    if (SDL_LockSurface(surface) == 0) {
        // Get dominant color from left edge
        *left_color = get_dominant_color(surface, 0, 0, edge_width, height);
        
        // Get dominant color from right edge
        *right_color = get_dominant_color(surface, width - edge_width, 0, edge_width, height);

        SDL_UnlockSurface(surface);
    } else {
        fprintf(stderr, "Failed to lock surface for pixel access: %s\n", SDL_GetError());
    }
}

// Function to render text as a texture
static SDL_Texture* render_text(const char *text, SDL_Color color) {
    if (!viewer.font || !text) return NULL;

    SDL_Surface *surface = TTF_RenderText_Blended(viewer.font, text, color);
    if (!surface) {
        fprintf(stderr, "Failed to render text: %s\n", TTF_GetError());
        return NULL;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(viewer.renderer, surface);
    SDL_FreeSurface(surface);

    if (!texture) {
        fprintf(stderr, "Failed to create texture from text: %s\n", SDL_GetError());
    }

    return texture;
}

// Function to select monitor and get its position
static bool select_monitor(int monitor_index, int *x, int *y) {
    int num_displays = SDL_GetNumVideoDisplays();
    if (num_displays <= 0) {
        fprintf(stderr, "No video displays available: %s\n", SDL_GetError());
        return false;
    }

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
static void create_high_quality_texture(SDL_Renderer *renderer, ImageEntry *image) {
    // Load the image as a surface
    image->surface = IMG_Load(image->path);
    if (!image->surface) {
        fprintf(stderr, "Failed to load image %s: %s\n", image->path, IMG_GetError());
        return;
    }

    // Detect and crop white borders
    int left = 0, right = image->surface->w - 1;
    int top = 0, bottom = image->surface->h - 1;
    int threshold = 240; // Threshold for considering a pixel "white" (0-255)
    int required_non_white = 3; // Number of non-white pixels required to stop scanning
    bool border_found = false;
    
    // Lock surface for pixel access
    if (SDL_LockSurface(image->surface) == 0) {
        SDL_PixelFormat *fmt = image->surface->format;
        int bpp = fmt->BytesPerPixel;
        Uint8 *pixels = (Uint8*)image->surface->pixels;
        int pitch = image->surface->pitch;
        
        // Scan from left edge inward
        border_found = true;
        for (left = 0; left < image->surface->w / 2; left++) {
            int non_white_count = 0;
            
            for (int y = 0; y < image->surface->h; y += 2) { // Sample every other pixel for speed
                Uint32 pixel = 0;
                Uint8 *p = pixels + y * pitch + left * bpp;
                
                switch (bpp) {
                    case 1: pixel = *p; break;
                    case 2: pixel = *(Uint16*)p; break;
                    case 3: 
                        #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                            pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                        #else
                            pixel = p[0] | p[1] << 8 | p[2] << 16; 
                        #endif
                        break;
                    case 4: pixel = *(Uint32*)p; break;
                }
                
                Uint8 r, g, b;
                SDL_GetRGB(pixel, fmt, &r, &g, &b);
                
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
                Uint32 pixel = 0;
                Uint8 *p = pixels + y * pitch + right * bpp;
                
                switch (bpp) {
                    case 1: pixel = *p; break;
                    case 2: pixel = *(Uint16*)p; break;
                    case 3: 
                        #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                            pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                        #else
                            pixel = p[0] | p[1] << 8 | p[2] << 16; 
                        #endif
                        break;
                    case 4: pixel = *(Uint32*)p; break;
                }
                
                Uint8 r, g, b;
                SDL_GetRGB(pixel, fmt, &r, &g, &b);
                
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
                Uint32 pixel = 0;
                Uint8 *p = pixels + top * pitch + x * bpp;
                
                switch (bpp) {
                    case 1: pixel = *p; break;
                    case 2: pixel = *(Uint16*)p; break;
                    case 3: 
                        #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                            pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                        #else
                            pixel = p[0] | p[1] << 8 | p[2] << 16; 
                        #endif
                        break;
                    case 4: pixel = *(Uint32*)p; break;
                }
                
                Uint8 r, g, b;
                SDL_GetRGB(pixel, fmt, &r, &g, &b);
                
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
                Uint32 pixel = 0;
                Uint8 *p = pixels + bottom * pitch + x * bpp;
                
                switch (bpp) {
                    case 1: pixel = *p; break;
                    case 2: pixel = *(Uint16*)p; break;
                    case 3: 
                        #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                            pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                        #else
                            pixel = p[0] | p[1] << 8 | p[2] << 16; 
                        #endif
                        break;
                    case 4: pixel = *(Uint32*)p; break;
                }
                
                Uint8 r, g, b;
                SDL_GetRGB(pixel, fmt, &r, &g, &b);
                
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
        
        SDL_UnlockSurface(image->surface);
    }
    
    // Create a cropped surface only if we found significant borders
    SDL_Surface *cropped_surface = image->surface;
    int crop_width = right - left + 1;
    int crop_height = bottom - top + 1;
    
    // Only crop if we found borders and they're significant (at least 10 pixels on any side)
    bool should_crop = false;
    
    // Check if any edge has a significant border
    if (left > 10 || (image->surface->w - 1 - right) > 10 || 
        top > 10 || (image->surface->h - 1 - bottom) > 10) {
        should_crop = true;
    }
    
    // Don't crop if it would remove too much of the image
    float crop_ratio = ((float)crop_width * crop_height) / 
                       ((float)image->surface->w * image->surface->h);
    if (crop_ratio < 0.5) { // Don't crop if we'd lose more than half the image
        should_crop = false;
    }
    
    if (should_crop && border_found) {
        // Create a new surface for the cropped image
        cropped_surface = SDL_CreateRGBSurfaceWithFormat(
            0, crop_width, crop_height,
            image->surface->format->BitsPerPixel,
            image->surface->format->format
        );
        
        if (cropped_surface) {
            // Set up source and destination rectangles
            SDL_Rect src_rect = { left, top, crop_width, crop_height };
            SDL_Rect dst_rect = { 0, 0, crop_width, crop_height };

            // Copy the cropped portion
            SDL_BlitSurface(image->surface, &src_rect, cropped_surface, &dst_rect);
            
            // We're done with the original surface
            SDL_FreeSurface(image->surface);
        } else {
            // Failed to create the new surface, stick with the original
            cropped_surface = image->surface;
        }
    }

    image->surface = cropped_surface; // Update the image entry with the new surface
    // Create the texture from the potentially cropped surface
    image->texture = SDL_CreateTextureFromSurface(renderer, image->surface);
    if (!image->texture) {
        fprintf(stderr, "Failed to create texture: %s\n", SDL_GetError());
    }
}

// Progress callback function for updating the progress bar
static void update_progress(float progress, const char *message) {
    // Update the progress bar
    progress_bar_update(progress, message);
}
