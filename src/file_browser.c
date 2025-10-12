#include "file_browser.h"
#include "comic_viewer.h"
#include "comic_loaders.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Use public wrappers exposed by comic_viewer
extern SDL_Texture *viewer_render_text(const char *text, SDL_Color color);
extern void viewer_init_view(ImageView *view);
extern bool viewer_has_current_view(void);

// Local state
typedef struct FileBrowserState
{
    bool active;
    char current_path[PATH_MAX];
    char **entries;
    int *is_dir;
    int entry_count;
    int selected;
    int scroll_offset;
    int max_visible;
} FileBrowserState;

static FileBrowserState fb = {0};

// Remember last opened state so reopen restores it
static char fb_last_path[PATH_MAX] = "";
static int fb_last_selected = 0;
static int fb_last_scroll = 0;
static bool fb_have_last = false;

static void fb_free_entries(void)
{
    if (fb.entries)
    {
        for (int i = 0; i < fb.entry_count; i++)
            free(fb.entries[i]);
        free(fb.entries);
        fb.entries = NULL;
    }
    free(fb.is_dir);
    fb.is_dir = NULL;
    fb.entry_count = 0;
    fb.selected = 0;
    fb.scroll_offset = 0;
}

// Normalize a path in-place: remove trailing slashes except for root "/"
static void fb_normalize_path(char *p)
{
    if (!p || !*p) return;
    size_t len = strlen(p);
    while (len > 1 && p[len - 1] == '/') {
        p[len - 1] = '\0';
        len--;
    }
}

static bool fb_is_supported(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return false;
    dot++;
    return strcasecmp(dot, "cbz") == 0 || strcasecmp(dot, "zip") == 0 ||
           strcasecmp(dot, "cbr") == 0 || strcasecmp(dot, "rar") == 0 ||
           strcasecmp(dot, "pdf") == 0 || is_image_file(name);
}

static void fb_scan(void)
{
    fb_free_entries();
    DIR *d = opendir(fb.current_path);
    if (!d)
        return;
    size_t cap = 128, count = 0;
    char **names = malloc(cap * sizeof(char *));
    int *dirs = malloc(cap * sizeof(int));
    if (!names || !dirs)
    {
        closedir(d);
        free(names);
        free(dirs);
        return;
    }
    names[count] = strdup("..");
    dirs[count] = 1;
    count++;
    struct dirent *ent;
    while ((ent = readdir(d)))
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
    char full[PATH_MAX];
    full[0] = '\0';
    strncpy(full, fb.current_path, sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    strncat(full, "/", sizeof(full) - strlen(full) - 1);
    strncat(full, ent->d_name, sizeof(full) - strlen(full) - 1);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode) || fb_is_supported(ent->d_name))
        {
            if (count == cap)
            {
                cap *= 2;
                names = realloc(names, cap * sizeof(char *));
                dirs = realloc(dirs, cap * sizeof(int));
            }
            names[count] = strdup(ent->d_name);
            dirs[count] = S_ISDIR(st.st_mode) ? 1 : 0;
            count++;
        }
    }
    closedir(d);
    int *idx = malloc(count * sizeof(int));
    if (!idx)
    {
        for (size_t i = 0; i < count; i++)
            free(names[i]);
        free(names);
        free(dirs);
        return;
    }
    for (size_t i = 0; i < count; i++)
        idx[i] = (int)i;
    for (size_t i = 0; i < count; i++)
        for (size_t j = i + 1; j < count; j++)
            if ((dirs[idx[i]] < dirs[idx[j]]) || (dirs[idx[i]] == dirs[idx[j]] && strcasecmp(names[idx[i]], names[idx[j]]) > 0))
            {
                int t = idx[i];
                idx[i] = idx[j];
                idx[j] = t;
            }
    fb.entries = malloc(count * sizeof(char *));
    fb.is_dir = malloc(count * sizeof(int));
    if (!fb.entries || !fb.is_dir)
    {
        fb_free_entries();
        for (size_t i = 0; i < count; i++)
            free(names[i]);
        free(names);
        free(dirs);
        free(idx);
        return;
    }
    for (size_t i = 0; i < count; i++)
    {
        fb.entries[i] = names[idx[i]];
        fb.is_dir[i] = dirs[idx[i]];
    }

    fb.entry_count = (int)count;
    fb.selected = 0;
    fb.scroll_offset = 0;
    fb.max_visible = 24; // default, will be updated on render
    free(idx);
    free(names);
    free(dirs);
}

// Recompute max visible lines based on current window height and text height
static void fb_update_max_visible(void) {
    if (viewer.window_height <= 0) return;
    // Panel vertical padding: 40 top + 40 bottom, plus inner padding 20
    float panel_h = (float)viewer.window_height - 80.0f;
    if (panel_h <= 10.0f) { fb.max_visible = 1; return; }
    // Measure a sample text height and the path display height
    SDL_Color sample_color = {220,220,220,255};
    SDL_Texture *tmp = viewer_render_text("Ag", sample_color);
    SDL_Texture *pth_tmp = viewer_render_text(fb.current_path[0] ? fb.current_path : "Path: /", sample_color);
    int line_h = 16; // fallback
    int path_h = 0;
    if (tmp) {
        float twf, thf; SDL_GetTextureSize(tmp, &twf, &thf);
        line_h = (int)(thf + 4.0f); // add small spacing
        SDL_DestroyTexture(tmp);
    }
    if (pth_tmp) {
        float twf, thf; SDL_GetTextureSize(pth_tmp, &twf, &thf);
        path_h = (int)(thf + 4.0f);
        SDL_DestroyTexture(pth_tmp);
    }
    if (line_h <= 0) line_h = 16;
    // Reserve space at bottom for the path line plus a small margin (10px)
    float usable_h = panel_h - (float)path_h - 10.0f;
    if (usable_h < (float)line_h) {
        // Ensure at least one line and keep path visible
        fb.max_visible = 1;
        return;
    }
    int maxv = (int)floorf(usable_h / (float)line_h);
    if (maxv < 1) maxv = 1;
    fb.max_visible = maxv;
    // Ensure scroll_offset stays sensible
    if (fb.scroll_offset < 0) fb.scroll_offset = 0;
    if (fb.scroll_offset > fb.entry_count - 1) fb.scroll_offset = fb.entry_count > 0 ? fb.entry_count - 1 : 0;
}

bool file_browser_is_active(void) { return fb.active; }

void file_browser_open(const char *path)
{
    if (path && *path) {
        strncpy(fb.current_path, path, sizeof(fb.current_path) - 1);
    } else if (fb_have_last) {
        // restore last path when reopening without explicit path
        strncpy(fb.current_path, fb_last_path, sizeof(fb.current_path) - 1);
    } else {
        getcwd(fb.current_path, sizeof(fb.current_path));
    }
    fb.current_path[sizeof(fb.current_path) - 1] = '\0';
    fb.active = true;
    fb_scan();
    // Restore selection/scroll if we have previous state
    if (fb_have_last) {
        if (fb_last_selected < fb.entry_count) fb.selected = fb_last_selected;
        else fb.selected = fb.entry_count > 0 ? fb.entry_count - 1 : 0;
        fb.scroll_offset = fb_last_scroll;
        // Clamp scroll_offset
        if (fb.scroll_offset < 0) fb.scroll_offset = 0;
        if (fb.scroll_offset > fb.selected) fb.scroll_offset = fb.selected;
    }
}

void file_browser_close(void)
{
    fb.active = false;
    // Save current state for next open
    strncpy(fb_last_path, fb.current_path, sizeof(fb_last_path)-1);
    fb_last_path[sizeof(fb_last_path)-1] = '\0';
    fb_last_selected = fb.selected;
    fb_last_scroll = fb.scroll_offset;
    fb_have_last = true;
    fb_free_entries();
}

static void fb_unload_current_comic(void)
{
    for (int i = 0; i < viewer.image_count; i++)
    {
        if (viewer.images[i].surface)
        {
            SDL_DestroySurface(viewer.images[i].surface);
            viewer.images[i].surface = NULL;
        }
        free(viewer.images[i].path);
        viewer.images[i].path = NULL;
        if (viewer.images[i].bitmap)
        {
            FreeImage_Unload(viewer.images[i].bitmap);
            viewer.images[i].bitmap = NULL;
        }
    }
    ImageView *v = viewer.first_view;
    while (v)
    {
        ImageView *n = v->next;
        if (v->texture)
            SDL_DestroyTexture(v->texture);
        free(v);
        v = n;
    }
    viewer.first_view = viewer.current_view_node = NULL;
    viewer.view_count = 0;
    viewer.current_view_index = 0;
    if (viewer.archive)
    {
        archive_close(viewer.archive);
        viewer.archive = NULL;
    }
    free(viewer.source_path);
    viewer.source_path = NULL;
    viewer.image_count = 0;
}

// Move up one directory and try to select the child folder we came from
static void fb_go_up_and_select_child(const char *child_name) {
    // compute parent path from current (normalize to avoid trailing slash issues)
    char parent[PATH_MAX];
    strncpy(parent, fb.current_path, sizeof(parent)-1);
    parent[sizeof(parent)-1] = '\0';
    fb_normalize_path(parent);
    char *slash = strrchr(parent, '/');
    if (!slash) return;
    if (slash == parent) {
        // already at root - keep as '/'
        parent[1] = '\0';
    } else {
        *slash = '\0';
    }
    // set current path to parent and rescan
    strncpy(fb.current_path, parent, sizeof(fb.current_path)-1);
    fb.current_path[sizeof(fb.current_path)-1] = '\0';
    fb_normalize_path(fb.current_path);
    fb_scan();

    // If child name provided, try to find and select it (directories only)
    if (child_name && *child_name) {
        for (int i = 0; i < fb.entry_count; ++i) {
            if (fb.is_dir[i] && strcmp(fb.entries[i], child_name) == 0) {
                fb.selected = i;
                // ensure it's visible
                if (fb.selected < fb.scroll_offset) fb.scroll_offset = fb.selected;
                else {
                    int bottom = fb.scroll_offset + fb.max_visible - 1;
                    if (fb.selected > bottom) {
                        fb.scroll_offset = fb.selected - (fb.max_visible - 1);
                        if (fb.scroll_offset < 0) fb.scroll_offset = 0;
                    }
                }
                return;
            }
        }
    }
    // fallback: clamp selection
    if (fb.selected >= fb.entry_count) fb.selected = fb.entry_count > 0 ? fb.entry_count - 1 : 0;
    if (fb.scroll_offset > fb.selected) fb.scroll_offset = fb.selected;
}

static void fb_activate_selected(void)
{
    if (fb.entry_count == 0)
        return;
    const char *name = fb.entries[fb.selected];
    if (strcmp(name, "..") == 0)
    {
        // derive child name (last component) to select in parent
        char child[PATH_MAX] = "";
        // normalize before extracting last component to avoid trailing slash issues
        fb_normalize_path(fb.current_path);
        char *slash = strrchr(fb.current_path, '/');
        if (slash && *(slash + 1)) strncpy(child, slash + 1, sizeof(child)-1);
        fb_go_up_and_select_child(child);
        return;
    }
    char full[PATH_MAX];
    full[0] = '\0';
    strncpy(full, fb.current_path, sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    strncat(full, "/", sizeof(full) - strlen(full) - 1);
    strncat(full, name, sizeof(full) - strlen(full) - 1);
    struct stat st;
    if (stat(full, &st) != 0)
        return;
    if (S_ISDIR(st.st_mode))
    {
        strncpy(fb.current_path, full, sizeof(fb.current_path) - 1);
        fb.current_path[sizeof(fb.current_path) - 1] = '\0';
        fb_scan();
    }
    else if (fb_is_supported(name))
    {
        fb_unload_current_comic();

        // if file is an image, we open the parent folder
        int opened;
        if (is_image_file(full)) {
            opened = comic_viewer_load_and_display(fb.current_path, full);
        } else {
            // it's an archive file, open it directly
            opened = comic_viewer_load(full);
        }

        if (opened && viewer_has_current_view())
            viewer_init_view(viewer.current_view_node);
        file_browser_close();
    }
}

void file_browser_handle_key(SDL_Keycode key)
{
    if (!fb.active)
        return;
    switch (key)
    {
    case SDLK_ESCAPE:
        file_browser_close();
        break;
    case SDLK_UP:
        if (fb.selected > 0)
        {
            fb.selected--;
            if (fb.selected < fb.scroll_offset)
                fb.scroll_offset = fb.selected;
        }
        break;
    case SDLK_DOWN:
        if (fb.selected < fb.entry_count - 1)
        {
            fb.selected++;
            int bottom = fb.scroll_offset + fb.max_visible - 1;
            if (fb.selected > bottom)
                fb.scroll_offset++;
        }
        break;
    case SDLK_PAGEUP:
        fb.selected -= fb.max_visible;
        if (fb.selected < 0)
            fb.selected = 0;
        if (fb.selected < fb.scroll_offset)
            fb.scroll_offset = fb.selected;
        break;
    case SDLK_PAGEDOWN:
        fb.selected += fb.max_visible;
        if (fb.selected >= fb.entry_count)
            fb.selected = fb.entry_count - 1;
        while (fb.selected >= fb.scroll_offset + fb.max_visible)
            fb.scroll_offset++;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        fb_activate_selected();
        break;
    case SDLK_RIGHT:
        fb_activate_selected();
        break;
    case SDLK_HOME:
        // Jump to first entry
        fb.selected = 0;
        fb.scroll_offset = 0;
        break;
    case SDLK_END:
        // Jump to last entry
        if (fb.entry_count > 0) {
            fb.selected = fb.entry_count - 1;
            // Scroll so selected is visible at bottom
            fb.scroll_offset = fb.selected - (fb.max_visible - 1);
            if (fb.scroll_offset < 0) fb.scroll_offset = 0;
        }
        break;
    case SDLK_BACKSPACE:
        {
            // Determine child name (last component) before moving up
            char child[PATH_MAX] = "";
            // normalize before extracting last component to avoid trailing slash issues
            fb_normalize_path(fb.current_path);
            char *slash = strrchr(fb.current_path, '/');
            if (slash && *(slash + 1)) {
                strncpy(child, slash + 1, sizeof(child) - 1);
                child[sizeof(child) - 1] = '\0';
            }
            fb_go_up_and_select_child(child);
        }
        break;
    default:
        break;
    }
}

void file_browser_render(void)
{
    if (!fb.active)
        return;
    fb_update_max_visible();
    SDL_SetRenderDrawBlendMode(viewer.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(viewer.renderer, 0, 0, 0, 200);
    SDL_FRect panel = {40, 40, viewer.window_width - 80.0f, viewer.window_height - 80.0f};
    SDL_RenderFillRect(viewer.renderer, &panel);
    int start = fb.scroll_offset;
    int end = start + fb.max_visible;
    if (end > fb.entry_count)
        end = fb.entry_count;
    float y = panel.y + 10;
    for (int i = start; i < end; i++)
    {
    char line[PATH_MAX + 10];
    line[0] = '\0';
    line[0] = (i == fb.selected) ? '>' : ' ';
    line[1] = ' ';
    line[2] = '\0';
    strncat(line, fb.entries[i], sizeof(line) - strlen(line) - 1);
    if (fb.is_dir[i]) strncat(line, "/", sizeof(line) - strlen(line) - 1);
        SDL_Color color = {220, 220, 220, 255};
        if (fb.is_dir[i])
        {
            color.r = 180;
            color.g = 200;
            color.b = 255;
        }
        if (i == fb.selected)
        {
            color.r = 255;
            color.g = 255;
            color.b = 0;
        }
        SDL_Texture *txt = viewer_render_text(line, color);
        if (txt)
        {
            float tw, th;
            SDL_GetTextureSize(txt, &tw, &th);
            SDL_FRect r = {panel.x + 10, y, tw, th};
            SDL_RenderTexture(viewer.renderer, txt, NULL, &r);
            SDL_DestroyTexture(txt);
            y += th + 4;
        }
    }
    char path_line[PATH_MAX + 32];
    snprintf(path_line, sizeof(path_line), "Path: %s", fb.current_path);
    SDL_Texture *pth = viewer_render_text(path_line, (SDL_Color){180, 180, 180, 255});
    if (pth)
    {
        float tw, th;
        SDL_GetTextureSize(pth, &tw, &th);
        SDL_FRect r = {panel.x + 10, panel.y + panel.h - th - 10, tw, th};
        SDL_RenderTexture(viewer.renderer, pth, NULL, &r);
        SDL_DestroyTexture(pth);
    }
}
