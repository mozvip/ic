// file_browser.h - minimal in-app file browser API
#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <stdbool.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize and open browser at given path (NULL for current working directory)
void file_browser_open(const char *path);
// Close browser and free internal resources
void file_browser_close(void);
// Handle key input when active
void file_browser_handle_key(SDL_Keycode key);
// Render overlay (call after normal scene render)
void file_browser_render(void);
// Returns true if browser overlay active
bool file_browser_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // FILE_BROWSER_H