#ifndef LINUX_DESKTOP_H
#define LINUX_DESKTOP_H

#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IC_APPLICATION_ID "io.github.mozvip.ic"
TTF_Font *linux_desktop_open_font(float size);
bool linux_desktop_trash_file(const char *path, char *error, size_t error_size);
void linux_desktop_init(void);
void linux_desktop_show_controls(void);
void linux_desktop_pump(void);
void linux_desktop_cleanup(void);

#ifdef __cplusplus
}
#endif
#endif