#ifndef TEMP_UTILS_H
#define TEMP_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Build and ensure the app temp root exists: <SDL_GetPrefPath>/tmp
bool temp_utils_get_root(char *out_root, size_t out_size);

// Create a unique temporary directory in the app temp root.
bool temp_utils_create_dir(char *out_dir, size_t out_size, const char *dir_template);

// Build a unique temporary file path in the app temp root.
bool temp_utils_build_file_path(char *out_path, size_t out_size, const char *prefix, const char *suffix);

#ifdef __cplusplus
}
#endif

#endif // TEMP_UTILS_H
