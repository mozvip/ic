#ifndef EDGES_H
#define EDGES_H

#include "comic_viewer.h"
#include <SDL3/SDL.h>

void render_horizontal_gradient_hsl(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color edge_color_rgb, bool edge_color_is_on_left_of_fill);
void analyze_image_left_edge(SDL_Surface *surface, SDL_FRect *crop_rect, SDL_Color *left_color);
void analyze_image_right_edge(SDL_Surface *surface, SDL_FRect *crop_rect, SDL_Color *right_color);

#endif // EDGES_H
