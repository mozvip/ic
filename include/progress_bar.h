/**
 * progress_bar.h
 * Header file for progress bar implementation
 */

#ifndef PROGRESS_BAR_H
#define PROGRESS_BAR_H

#include <stdbool.h>
#include <SDL2/SDL.h>

// Initialize the progress bar
bool progress_bar_init(SDL_Renderer *renderer);

// Update the progress bar with a new value
void progress_bar_update(float progress, const char *message);

// Render the progress bar
void progress_bar_render(void);

// Clean up resources
void progress_bar_cleanup(void);

#endif // PROGRESS_BAR_H