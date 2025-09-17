#ifndef EDGES_H
#define EDGES_H

#include "comic_viewer.h"
#include <SDL3/SDL.h>

void analyze_image_left_edge(SDL_Surface *surface, SDL_FRect *crop_rect, SDL_Color *left_color);
void analyze_image_right_edge(SDL_Surface *surface, SDL_FRect *crop_rect, SDL_Color *right_color);

#endif // EDGES_H
