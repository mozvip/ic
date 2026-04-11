#include "temp_utils.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool temp_utils_get_root(char *out_root, size_t out_size) {
    if (!out_root || out_size == 0) return false;

    char *pref_path = SDL_GetPrefPath("mozvip", "ic");
    if (!pref_path) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to get preference path: %s", SDL_GetError());
        return false;
    }

    int n = snprintf(out_root, out_size, "%stmp", pref_path);
    SDL_free(pref_path);
    if (n < 0 || (size_t)n >= out_size) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Temp root path too long");
        return false;
    }

    if (mkdir(out_root, 0700) != 0 && errno != EEXIST) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create temp root directory: %s", out_root);
        return false;
    }

    return true;
}

bool temp_utils_create_dir(char *out_dir, size_t out_size, const char *dir_template) {
    if (!out_dir || out_size == 0 || !dir_template || !*dir_template) return false;

    char temp_root[512];
    if (!temp_utils_get_root(temp_root, sizeof(temp_root))) {
        return false;
    }

    int n = snprintf(out_dir, out_size, "%s/%s", temp_root, dir_template);
    if (n < 0 || (size_t)n >= out_size) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Temporary directory path too long");
        return false;
    }

    if (mkdtemp(out_dir) == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create temporary directory");
        return false;
    }

    return true;
}

bool temp_utils_build_file_path(char *out_path, size_t out_size, const char *prefix, const char *suffix) {
    if (!out_path || out_size == 0 || !prefix || !*prefix) return false;

    char temp_root[512];
    if (!temp_utils_get_root(temp_root, sizeof(temp_root))) {
        return false;
    }

    if (!suffix) suffix = "";

    int n = snprintf(out_path, out_size, "%s/%s_%llu%s",
                     temp_root,
                     prefix,
                     (unsigned long long)SDL_GetPerformanceCounter(),
                     suffix);
    if (n < 0 || (size_t)n >= out_size) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Temporary file path too long");
        return false;
    }

    return true;
}
