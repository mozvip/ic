#include "file_browser.h"

#include "comic_loaders.h"
#include "comic_viewer.h"
#include "imgui.h"

#include <SDL3/SDL.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct FileBrowserState {
    bool active;
    char current_path[PATH_MAX];
    std::vector<std::string> entries;
    std::vector<int> is_dir;
    std::vector<long long> size_bytes;
    int selected;
    bool scroll_to_selected;
    int view_mode;
} FileBrowserState;

static FileBrowserState fb = {};

enum FbViewMode {
    FB_VIEW_LIST = 0,
    FB_VIEW_COMPACT = 1,
    FB_VIEW_GRID = 2,
    FB_VIEW_COUNT = 3,
};

static char fb_last_path[PATH_MAX] = "";
static int fb_last_selected = 0;
static bool fb_have_last = false;

static void fb_normalize_path(char *p) {
    if (!p || !*p) {
        return;
    }

    size_t len = strlen(p);
    while (len > 1 && p[len - 1] == '/') {
        p[len - 1] = '\0';
        len--;
    }
}

static bool fb_is_supported(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }

    dot++;
    return strcasecmp(dot, "cbz") == 0 || strcasecmp(dot, "zip") == 0 ||
           strcasecmp(dot, "cbr") == 0 || strcasecmp(dot, "rar") == 0 ||
           strcasecmp(dot, "pdf") == 0 || is_image_file(name);
}

static const char *fb_view_mode_name(int mode) {
    switch (mode) {
    case FB_VIEW_COMPACT:
        return "Compact";
    case FB_VIEW_GRID:
        return "Grid";
    case FB_VIEW_LIST:
    default:
        return "List";
    }
}

static void fb_format_size(long long bytes, char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }

    if (bytes < 0) {
        out[0] = '\0';
        return;
    }

    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = (double)bytes;
    int unit = 0;
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }

    if (unit == 0) {
        snprintf(out, out_size, "%lld %s", bytes, units[unit]);
    } else {
        snprintf(out, out_size, "%.1f %s", size, units[unit]);
    }
}

static void fb_scan(void) {
    fb.entries.clear();
    fb.is_dir.clear();
    fb.size_bytes.clear();

    DIR *d = opendir(fb.current_path);
    if (!d) {
        return;
    }

    std::vector<std::string> names;
    std::vector<int> dirs;
    std::vector<long long> sizes;

    names.emplace_back("..");
    dirs.push_back(1);
    sizes.push_back(-1);

    struct dirent *ent = nullptr;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        char full[PATH_MAX];
        full[0] = '\0';
        strncpy(full, fb.current_path, sizeof(full) - 1);
        full[sizeof(full) - 1] = '\0';
        strncat(full, "/", sizeof(full) - strlen(full) - 1);
        strncat(full, ent->d_name, sizeof(full) - strlen(full) - 1);

        struct stat st;
        if (stat(full, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode) || fb_is_supported(ent->d_name)) {
            names.emplace_back(ent->d_name);
            dirs.push_back(S_ISDIR(st.st_mode) ? 1 : 0);
            sizes.push_back(S_ISDIR(st.st_mode) ? -1 : (long long)st.st_size);
        }
    }

    closedir(d);

    std::vector<int> idx(names.size());
    for (size_t i = 0; i < idx.size(); i++) {
        idx[i] = (int)i;
    }

    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        if (dirs[a] != dirs[b]) {
            return dirs[a] > dirs[b];
        }
        return strcasecmp(names[a].c_str(), names[b].c_str()) < 0;
    });

    fb.entries.reserve(idx.size());
    fb.is_dir.reserve(idx.size());
    fb.size_bytes.reserve(idx.size());
    for (int i : idx) {
        fb.entries.push_back(names[(size_t)i]);
        fb.is_dir.push_back(dirs[(size_t)i]);
        fb.size_bytes.push_back(sizes[(size_t)i]);
    }

    fb.selected = 0;
    fb.scroll_to_selected = true;
}

extern "C" bool file_browser_is_active(void) {
    return fb.active;
}

extern "C" void file_browser_open(const char *path) {
    if (path && *path) {
        strncpy(fb.current_path, path, sizeof(fb.current_path) - 1);
    } else if (fb_have_last) {
        strncpy(fb.current_path, fb_last_path, sizeof(fb.current_path) - 1);
    } else {
        getcwd(fb.current_path, sizeof(fb.current_path));
    }

    fb.current_path[sizeof(fb.current_path) - 1] = '\0';
    fb.active = true;

    if (fb.view_mode < FB_VIEW_LIST || fb.view_mode >= FB_VIEW_COUNT) {
        fb.view_mode = FB_VIEW_LIST;
    }

    fb_scan();

    if (fb_have_last && !fb.entries.empty()) {
        if (fb_last_selected < (int)fb.entries.size()) {
            fb.selected = fb_last_selected;
        } else {
            fb.selected = (int)fb.entries.size() - 1;
        }
        fb.scroll_to_selected = true;
    }
}

extern "C" void file_browser_close(void) {
    fb.active = false;

    strncpy(fb_last_path, fb.current_path, sizeof(fb_last_path) - 1);
    fb_last_path[sizeof(fb_last_path) - 1] = '\0';
    fb_last_selected = fb.selected;
    fb_have_last = true;

    fb.entries.clear();
    fb.is_dir.clear();
    fb.size_bytes.clear();
    fb.selected = 0;
    fb.scroll_to_selected = false;
}

static void fb_unload_current_comic(void) {
    for (int i = 0; i < viewer.image_count; i++) {
        if (viewer.images[i].surface) {
            SDL_DestroySurface(viewer.images[i].surface);
            viewer.images[i].surface = NULL;
        }
        free(viewer.images[i].path);
        viewer.images[i].path = NULL;
    }

    ImageView *v = viewer.first_view;
    while (v) {
        ImageView *n = v->next;
        if (v->texture) {
            SDL_DestroyTexture(v->texture);
        }
        free(v);
        v = n;
    }

    viewer.first_view = viewer.current_view_node = NULL;
    viewer.view_count = 0;
    viewer.current_view_index = 0;

    if (viewer.archive) {
        archive_close(viewer.archive);
        viewer.archive = NULL;
    }

    free(viewer.source_path);
    viewer.source_path = NULL;
    viewer.image_count = 0;
}

static void fb_go_up_and_select_child(const char *child_name) {
    char parent[PATH_MAX];
    strncpy(parent, fb.current_path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    fb_normalize_path(parent);

    char *slash = strrchr(parent, '/');
    if (!slash) {
        return;
    }

    if (slash == parent) {
        parent[1] = '\0';
    } else {
        *slash = '\0';
    }

    strncpy(fb.current_path, parent, sizeof(fb.current_path) - 1);
    fb.current_path[sizeof(fb.current_path) - 1] = '\0';
    fb_normalize_path(fb.current_path);
    fb_scan();

    if (child_name && *child_name) {
        for (int i = 0; i < (int)fb.entries.size(); i++) {
            if (fb.is_dir[(size_t)i] && strcmp(fb.entries[(size_t)i].c_str(), child_name) == 0) {
                fb.selected = i;
                fb.scroll_to_selected = true;
                return;
            }
        }
    }

    if (!fb.entries.empty() && fb.selected >= (int)fb.entries.size()) {
        fb.selected = (int)fb.entries.size() - 1;
    }
    fb.scroll_to_selected = true;
}

static void fb_activate_selected(void) {
    if (fb.entries.empty()) {
        return;
    }

    const char *name = fb.entries[(size_t)fb.selected].c_str();

    if (strcmp(name, "..") == 0) {
        char child[PATH_MAX] = "";
        fb_normalize_path(fb.current_path);
        char *slash = strrchr(fb.current_path, '/');
        if (slash && *(slash + 1)) {
            strncpy(child, slash + 1, sizeof(child) - 1);
            child[sizeof(child) - 1] = '\0';
        }
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
    if (stat(full, &st) != 0) {
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        strncpy(fb.current_path, full, sizeof(fb.current_path) - 1);
        fb.current_path[sizeof(fb.current_path) - 1] = '\0';
        fb_scan();
        return;
    }

    if (!fb_is_supported(name)) {
        return;
    }

    fb_unload_current_comic();

    int opened = 0;
    if (is_image_file(full)) {
        opened = comic_viewer_load_and_display(fb.current_path, full) ? 1 : 0;
    } else {
        opened = comic_viewer_load(full) ? 1 : 0;
    }

    if (opened && viewer_has_current_view()) {
        viewer_init_view(viewer.current_view_node);
    }

    file_browser_close();
}

extern "C" void file_browser_handle_key(SDL_Keycode key) {
    if (!fb.active) {
        return;
    }

    if (key == SDLK_ESCAPE) {
        file_browser_close();
    }
}

extern "C" void file_browser_render(void) {
    if (!fb.active) {
        return;
    }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);

    bool keep_open = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

    if (!ImGui::Begin("Open Comic", &keep_open, flags)) {
        ImGui::End();
        if (!keep_open) {
            file_browser_close();
        }
        return;
    }

    if (!keep_open) {
        ImGui::End();
        file_browser_close();
        return;
    }

    ImGui::TextUnformatted("Browse files and folders");
    ImGui::Separator();

    ImGui::TextWrapped("Path: %s", fb.current_path);
    ImGui::Spacing();

    const float footer_height = ImGui::GetFrameHeightWithSpacing() * 3.2f;
    ImGui::BeginChild("##browser_entries", ImVec2(0.0f, -footer_height), true, ImGuiWindowFlags_NoNav);

    bool activated = false;
    bool moved_selection = false;
    int grid_columns = 1;

    if (fb.view_mode == FB_VIEW_GRID) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImGuiStyle &style = ImGui::GetStyle();
        const float tile_width = 220.0f;
        float slot = tile_width + style.ItemSpacing.x;
        if (slot < 1.0f) {
            slot = 1.0f;
        }
        grid_columns = (int)((avail.x + style.ItemSpacing.x) / slot);
        if (grid_columns < 1) {
            grid_columns = 1;
        }
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_V)) {
            fb.view_mode = (fb.view_mode + 1) % FB_VIEW_COUNT;
        }
        if (fb.view_mode == FB_VIEW_GRID) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && fb.selected > 0) {
                fb.selected--;
                moved_selection = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && fb.selected + 1 < (int)fb.entries.size()) {
                fb.selected++;
                moved_selection = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && fb.selected > 0) {
                fb.selected -= grid_columns;
                if (fb.selected < 0) {
                    fb.selected = 0;
                }
                moved_selection = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && fb.selected + 1 < (int)fb.entries.size()) {
                fb.selected += grid_columns;
                if (fb.selected >= (int)fb.entries.size()) {
                    fb.selected = (int)fb.entries.size() - 1;
                }
                moved_selection = true;
            }
        } else {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && fb.selected > 0) {
                fb.selected--;
                moved_selection = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && fb.selected + 1 < (int)fb.entries.size()) {
                fb.selected++;
                moved_selection = true;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home) && !fb.entries.empty()) {
            fb.selected = 0;
            moved_selection = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End) && !fb.entries.empty()) {
            fb.selected = (int)fb.entries.size() - 1;
            moved_selection = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp) && !fb.entries.empty()) {
            float row_height = ImGui::GetTextLineHeightWithSpacing();
            if (fb.view_mode == FB_VIEW_GRID) {
                row_height = ImGui::GetFrameHeightWithSpacing() * 1.5f;
            }
            int visible = (int)(ImGui::GetWindowHeight() / row_height);
            if (visible < 1) {
                visible = 1;
            }
            if (fb.view_mode == FB_VIEW_GRID) {
                visible *= grid_columns;
            }
            fb.selected -= visible;
            if (fb.selected < 0) {
                fb.selected = 0;
            }
            moved_selection = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown) && !fb.entries.empty()) {
            float row_height = ImGui::GetTextLineHeightWithSpacing();
            if (fb.view_mode == FB_VIEW_GRID) {
                row_height = ImGui::GetFrameHeightWithSpacing() * 1.5f;
            }
            int visible = (int)(ImGui::GetWindowHeight() / row_height);
            if (visible < 1) {
                visible = 1;
            }
            if (fb.view_mode == FB_VIEW_GRID) {
                visible *= grid_columns;
            }
            fb.selected += visible;
            if (fb.selected >= (int)fb.entries.size()) {
                fb.selected = (int)fb.entries.size() - 1;
            }
            moved_selection = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            char child[PATH_MAX] = "";
            fb_normalize_path(fb.current_path);
            char *slash = strrchr(fb.current_path, '/');
            if (slash && *(slash + 1)) {
                strncpy(child, slash + 1, sizeof(child) - 1);
                child[sizeof(child) - 1] = '\0';
            }
            fb_go_up_and_select_child(child);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            activated = true;
        }
    }

    if (moved_selection) {
        fb.scroll_to_selected = true;
    }

    for (int i = 0; i < (int)fb.entries.size(); i++) {
        std::string label;
        if (fb.view_mode == FB_VIEW_GRID) {
            label = fb.is_dir[(size_t)i] ? "[DIR] " : "[FILE] ";
            label += fb.entries[(size_t)i];
        } else if (fb.view_mode == FB_VIEW_COMPACT) {
            label = fb.is_dir[(size_t)i] ? "D  " : "F  ";
            label += fb.entries[(size_t)i];
        } else {
            label = fb.entries[(size_t)i];
            if (fb.is_dir[(size_t)i]) {
                label += "/";
            } else {
                char size_text[32];
                fb_format_size(fb.size_bytes[(size_t)i], size_text, sizeof(size_text));
                if (size_text[0] != '\0') {
                    label += " (";
                    label += size_text;
                    label += ")";
                }
            }
        }

        bool selected = (i == fb.selected);
        ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_AllowDoubleClick;

        if (fb.view_mode == FB_VIEW_GRID) {
            if (i > 0 && (i % grid_columns) != 0) {
                ImGui::SameLine();
            }
            if (ImGui::Selectable(label.c_str(), selected, selectable_flags, ImVec2(220.0f, 0.0f))) {
                fb.selected = i;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    activated = true;
                }
            }
        } else {
            if (ImGui::Selectable(label.c_str(), selected, selectable_flags)) {
                fb.selected = i;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    activated = true;
                }
            }
        }

        if (selected) {
            if (fb.scroll_to_selected) {
                ImGui::SetScrollHereY(0.5f);
                ImGui::SetItemDefaultFocus();
            }
        }
    }

    fb.scroll_to_selected = false;

    ImGui::EndChild();

    if (activated) {
        fb_activate_selected();
    }

    if (!fb.active) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("List")) {
        fb.view_mode = FB_VIEW_LIST;
    }
    ImGui::SameLine();
    if (ImGui::Button("Compact")) {
        fb.view_mode = FB_VIEW_COMPACT;
    }
    ImGui::SameLine();
    if (ImGui::Button("Grid")) {
        fb.view_mode = FB_VIEW_GRID;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Mode: %s (V to cycle)", fb_view_mode_name(fb.view_mode));

    if (ImGui::Button("Open") && !fb.entries.empty()) {
        fb_activate_selected();
    }
    ImGui::SameLine();
    if (ImGui::Button("Up")) {
        char child[PATH_MAX] = "";
        fb_normalize_path(fb.current_path);
        char *slash = strrchr(fb.current_path, '/');
        if (slash && *(slash + 1)) {
            strncpy(child, slash + 1, sizeof(child) - 1);
            child[sizeof(child) - 1] = '\0';
        }
        fb_go_up_and_select_child(child);
    }
    ImGui::SameLine();
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        file_browser_close();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Double-click or press Enter to open");

    ImGui::End();
}
