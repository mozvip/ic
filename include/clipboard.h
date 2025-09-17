#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include "comic_viewer.h"
#include <SDL3/SDL.h>

// Copy currently displayed view (cropped, combined pages) to clipboard.
// Currently falls back to text if image data cannot be placed.
void copy_current_view_to_clipboard(void);

#endif // CLIPBOARD_H
