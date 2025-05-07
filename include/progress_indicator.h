#ifndef PROGRESS_INDICATOR_H
#define PROGRESS_INDICATOR_H

#include <SDL3/SDL.h>

void draw_progress_indicator(SDL_Renderer *renderer, float progress, int centerX, int centerY, int radius);

#endif // PROGRESS_INDICATOR_H
