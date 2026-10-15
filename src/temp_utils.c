#include "temp_utils.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>

static bool user_directory(const char *base, char *out, size_t size) {
    char *path = g_build_filename(base, "ic", NULL);
    bool result = strlen(path) < size && g_mkdir_with_parents(path, 0700) == 0;
    if (result) SDL_strlcpy(out, path, size);
    g_free(path);
    return result;
}

bool temp_utils_get_state_root(char *out_root, size_t out_size) {
    return out_root && out_size && user_directory(g_get_user_state_dir(), out_root, out_size);
}

bool temp_utils_get_root(char *out_root, size_t out_size) {
    return out_root && out_size && user_directory(g_get_user_cache_dir(), out_root, out_size);
}

static bool remove_contents(int directory_fd) {
    DIR *directory = fdopendir(directory_fd);
    if (!directory) {
        close(directory_fd);
        return false;
    }
    bool result = true;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat info;
        if (fstatat(dirfd(directory), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            result = false;
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            int child = openat(dirfd(directory), entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0 || !remove_contents(child) ||
                unlinkat(dirfd(directory), entry->d_name, AT_REMOVEDIR) != 0) result = false;
        } else if (unlinkat(dirfd(directory), entry->d_name, 0) != 0) {
            result = false;
        }
    }
    closedir(directory);
    return result;
}

bool temp_utils_remove_dir(const char *path) {
    char root[512];
    if (!path || !temp_utils_get_root(root, sizeof(root))) return false;
    size_t length = strlen(root);
    if (strncmp(path, root, length) || path[length] != '/') return false;
    const char *name = path + length + 1;
    if (!*name || !strcmp(name, ".") || !strcmp(name, "..") || strchr(name, '/')) return false;
    int parent = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent < 0) return false;
    int child = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    bool result = child < 0 ? errno == ENOENT : remove_contents(child);
    if (result && unlinkat(parent, name, AT_REMOVEDIR) != 0 && errno != ENOENT) result = false;
    close(parent);
    return result;
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
