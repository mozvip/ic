/* render.h - rendering public API for comic viewer */
#ifndef RENDER_H
#define RENDER_H

#include <SDL3/SDL.h>
#include "comic_viewer.h"

/* Render the current view (frame) */
void viewer_render_current_view(void);

/* Display progress / info overlay */
void viewer_display_info(void);

/* Draw cropping overlay if active (called by viewer_render_current_view internally) */
void viewer_render_cropping_overlay(void);

/* Render text helper exposed to other modules */
SDL_Texture* viewer_render_text(const char *text, SDL_Color color);

#endif // RENDER_H
