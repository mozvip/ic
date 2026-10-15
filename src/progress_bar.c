/**
 * progress_bar.c
 * Implementation of a simple progress bar
 */

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "progress_bar.h"
#include "linux_desktop.h"

// Progress bar properties
#define PROGRESS_BAR_HEIGHT 20
#define PROGRESS_BAR_PADDING 50
#define TEXT_PADDING 10

// Progress bar state
static struct {
    SDL_Renderer *renderer;
    TTF_Font *font;
    float progress;
    Uint64 last_render_ns;
    char message[256];
    int window_width;
    int window_height;
    bool initialized;
} progress_bar = {0};

bool progress_bar_init(SDL_Renderer *renderer) {
    if (!renderer) return false;
    
    // Get the window size
    int output_w, output_h;
    SDL_GetRenderOutputSize(renderer, &output_w, &output_h);
    progress_bar.window_width = output_w;
    progress_bar.window_height = output_h;
    
    progress_bar.font = linux_desktop_open_font(14);
    if (!progress_bar.font) {
        fprintf(stderr, "Warning: Failed to load font for progress bar\n");
        // Non-fatal, we'll just not show text
    }
    
    progress_bar.renderer = renderer;
    progress_bar.progress = 0.0f;
    progress_bar.last_render_ns = 0;
    strcpy(progress_bar.message, "Loading...");
    progress_bar.initialized = true;
    
    return true;
}

void progress_bar_update(float progress, const char *message) {
    if (!progress_bar.initialized) return;
    
    // Clamp progress value between 0 and 1
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    if (progress > 0.0f && progress < progress_bar.progress) progress = progress_bar.progress;
    
    progress_bar.progress = progress;
    if (message) {
        strncpy(progress_bar.message, message, sizeof(progress_bar.message) - 1);
        progress_bar.message[sizeof(progress_bar.message) - 1] = '\0';
    }
    
    Uint64 now = SDL_GetTicksNS();
    if (progress == 0.0f || progress == 1.0f ||
        now - progress_bar.last_render_ns >= SDL_NS_PER_SECOND / 60) {
        progress_bar_render();
    }
}

void progress_bar_render(void) {
    if (!progress_bar.initialized) return;
    SDL_RendererLogicalPresentation presentation;
    if (!SDL_GetRenderLogicalPresentation(progress_bar.renderer,
                                          &progress_bar.window_width, &progress_bar.window_height,
                                          &presentation) || presentation == SDL_LOGICAL_PRESENTATION_DISABLED) {
        if (!SDL_GetRenderOutputSize(progress_bar.renderer,
                                    &progress_bar.window_width, &progress_bar.window_height)) return;
    }
    
    // Clear the screen
    SDL_SetRenderDrawColor(progress_bar.renderer, 0, 0, 0, 255);
    SDL_RenderClear(progress_bar.renderer);
    
    // Draw progress bar background
    SDL_FRect bg_rect = {
        PROGRESS_BAR_PADDING,
        progress_bar.window_height / 2 - PROGRESS_BAR_HEIGHT / 2,
        progress_bar.window_width - (2 * PROGRESS_BAR_PADDING),
        PROGRESS_BAR_HEIGHT
    };
    
    SDL_SetRenderDrawColor(progress_bar.renderer, 50, 50, 50, 255);
    SDL_RenderFillRect(progress_bar.renderer, &bg_rect);
    
    // Draw progress bar fill
    SDL_FRect fill_rect = {
        PROGRESS_BAR_PADDING,
        progress_bar.window_height / 2 - PROGRESS_BAR_HEIGHT / 2,
        (progress_bar.window_width - (2 * PROGRESS_BAR_PADDING)) * progress_bar.progress,
        PROGRESS_BAR_HEIGHT
    };
    
    SDL_SetRenderDrawColor(progress_bar.renderer, 0, 150, 255, 255);
    SDL_RenderFillRect(progress_bar.renderer, &fill_rect);
    
    // Draw message (above the progress bar)
    if (progress_bar.font && progress_bar.message[0] != '\0') {
        SDL_Color text_color = {255, 255, 255, 255};
        SDL_Surface *text_surface = TTF_RenderText_Blended(
            progress_bar.font, progress_bar.message, 0, text_color);
        
        if (text_surface) {
            SDL_Texture *text_texture = SDL_CreateTextureFromSurface(
                progress_bar.renderer, text_surface);
            
            if (text_texture) {
                SDL_FRect text_rect = {
                    (progress_bar.window_width - text_surface->w) / 2,
                    progress_bar.window_height / 2 - PROGRESS_BAR_HEIGHT / 2 - text_surface->h - TEXT_PADDING,
                    (float)text_surface->w,
                    (float)text_surface->h
                };
                
                SDL_RenderTexture(progress_bar.renderer, text_texture, NULL, &text_rect);
                SDL_DestroyTexture(text_texture);
            }
            
            SDL_DestroySurface(text_surface);
        }
    }
    
    // Draw percentage text (inside the progress bar)
    char percentage[8];
    snprintf(percentage, sizeof(percentage), "%d%%", (int)(progress_bar.progress * 100));
    
    if (progress_bar.font) {
        SDL_Color percentage_color = {255, 255, 255, 255};
        SDL_Surface *percentage_surface = TTF_RenderText_Blended(
            progress_bar.font, percentage, 0, percentage_color);
        
        if (percentage_surface) {
            SDL_Texture *percentage_texture = SDL_CreateTextureFromSurface(
                progress_bar.renderer, percentage_surface);
            
            if (percentage_texture) {
                SDL_FRect percentage_rect = {
                    (progress_bar.window_width - percentage_surface->w) / 2,
                    progress_bar.window_height / 2 - percentage_surface->h / 2,
                    (float)percentage_surface->w,
                    (float)percentage_surface->h
                };
                
                SDL_RenderTexture(progress_bar.renderer, percentage_texture, NULL, &percentage_rect);
                SDL_DestroyTexture(percentage_texture);
            }
            
            SDL_DestroySurface(percentage_surface);
        }
    }
    
    // Present the renderer
    SDL_RenderPresent(progress_bar.renderer);
    progress_bar.last_render_ns = SDL_GetTicksNS();
    
    // Process events during rendering to keep the UI responsive
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            exit(0); // Allow user to exit during loading
        }
    }
}

void progress_bar_cleanup(void) {
    if (progress_bar.font) {
        TTF_CloseFont(progress_bar.font);
        progress_bar.font = NULL;
    }
    
    progress_bar.initialized = false;
}