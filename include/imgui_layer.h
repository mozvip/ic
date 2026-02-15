#pragma once

#include <stdbool.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

bool imgui_layer_init(SDL_Window *window, SDL_Renderer *renderer);
void imgui_layer_shutdown(void);

bool imgui_layer_is_initialized(void);

void imgui_layer_process_event(const SDL_Event *event);
void imgui_layer_new_frame(void);
void imgui_layer_render(void);

void imgui_layer_set_visible(bool visible);
bool imgui_layer_is_visible(void);

bool imgui_layer_wants_capture_keyboard(void);
bool imgui_layer_wants_capture_mouse(void);

void imgui_layer_set_status_message(const char *message);

#ifdef __cplusplus
}
#endif
