/* overlay.h - Overlay rendering definitions */

#ifndef OVERLAY_H
#define OVERLAY_H

#include <SDL3/SDL.h>
#include "comic_viewer.h"

/**
 * Render the background overlay based on the current mode.
 * 
 * @param renderer The SDL renderer
 * @param view The current view containing the image texture
 * @param display_width Width of the display area
 * @param display_height Height of the display area
 * @param content_start_x The x coordinate where the main content starts drawing
 * @param content_end_x The x coordinate where the main content ends drawing
 */
void render_overlay(SDL_Renderer *renderer, ImageView *view, 
                   float display_width, float display_height,
                   float content_start_x, float content_end_x);

#endif // OVERLAY_H
