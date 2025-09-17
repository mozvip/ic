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

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Use public wrappers exposed by comic_viewer
extern SDL_Texture* viewer_render_text(const char *text, SDL_Color color);
extern void viewer_init_view(ImageView *view);
extern bool viewer_has_current_view(void);

// Local state
typedef struct FileBrowserState {
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

static void fb_free_entries(void) {
    if (fb.entries) {
        for (int i=0;i<fb.entry_count;i++) free(fb.entries[i]);
        free(fb.entries); fb.entries = NULL;
    }
    free(fb.is_dir); fb.is_dir = NULL;
    fb.entry_count = 0; fb.selected = 0; fb.scroll_offset = 0;
}

static bool fb_is_supported(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return false; dot++;
    return strcasecmp(dot, "cbz")==0 || strcasecmp(dot, "zip")==0 ||
           strcasecmp(dot, "cbr")==0 || strcasecmp(dot, "rar")==0 ||
           strcasecmp(dot, "pdf")==0;
}

static void fb_scan(void) {
    fb_free_entries();
    DIR *d = opendir(fb.current_path);
    if (!d) return;
    size_t cap=128, count=0;
    char **names = malloc(cap*sizeof(char*));
    int *dirs = malloc(cap*sizeof(int));
    if (!names || !dirs) { closedir(d); free(names); free(dirs); return; }
    names[count]=strdup(".."); dirs[count]=1; count++;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".")==0 || strcmp(ent->d_name, "..")==0) continue;
        char full[PATH_MAX]; snprintf(full, sizeof(full), "%s/%s", fb.current_path, ent->d_name);
        struct stat st; if (stat(full,&st)!=0) continue;
        if (S_ISDIR(st.st_mode) || fb_is_supported(ent->d_name)) {
            if (count==cap) { cap*=2; names=realloc(names,cap*sizeof(char*)); dirs=realloc(dirs,cap*sizeof(int)); }
            names[count]=strdup(ent->d_name);
            dirs[count]= S_ISDIR(st.st_mode) ? 1:0; count++;
        }
    }
    closedir(d);
    int *idx = malloc(count*sizeof(int)); if (!idx) { for(size_t i=0;i<count;i++) free(names[i]); free(names); free(dirs); return; }
    for (size_t i=0;i<count;i++) idx[i]=(int)i;
    for (size_t i=0;i<count;i++) for (size_t j=i+1;j<count;j++) if ( (dirs[idx[i]] < dirs[idx[j]]) || (dirs[idx[i]]==dirs[idx[j]] && strcasecmp(names[idx[i]], names[idx[j]])>0) ) { int t=idx[i]; idx[i]=idx[j]; idx[j]=t; }
    fb.entries = malloc(count*sizeof(char*)); fb.is_dir = malloc(count*sizeof(int));
    if (!fb.entries || !fb.is_dir) { fb_free_entries(); for(size_t i=0;i<count;i++) free(names[i]); free(names); free(dirs); free(idx); return; }
    for (size_t i=0;i<count;i++){ fb.entries[i]=names[idx[i]]; fb.is_dir[i]=dirs[idx[i]]; }
    fb.entry_count=(int)count; fb.selected=0; fb.scroll_offset=0; fb.max_visible=24;
    free(idx); free(names); free(dirs);
}

bool file_browser_is_active(void) { return fb.active; }

void file_browser_open(const char *path) {
    if (path && *path) strncpy(fb.current_path, path, sizeof(fb.current_path)-1);
    else getcwd(fb.current_path, sizeof(fb.current_path));
    fb.current_path[sizeof(fb.current_path)-1]='\0';
    fb.active = true;
    fb_scan();
}

void file_browser_close(void) {
    fb.active = false;
    fb_free_entries();
}

static void fb_unload_current_comic(void) {
    for (int i=0;i<viewer.image_count;i++) {
        if (viewer.images[i].surface){ SDL_DestroySurface(viewer.images[i].surface); viewer.images[i].surface=NULL; }
        free(viewer.images[i].path); viewer.images[i].path=NULL;
        if (viewer.images[i].bitmap){ FreeImage_Unload(viewer.images[i].bitmap); viewer.images[i].bitmap=NULL; }
    }
    ImageView *v=viewer.first_view; while(v){ ImageView *n=v->next; if (v->texture) SDL_DestroyTexture(v->texture); free(v); v=n; }
    viewer.first_view=viewer.current_view_node=NULL; viewer.view_count=0; viewer.current_view_index=0;
    if (viewer.archive){ archive_close(viewer.archive); viewer.archive=NULL; }
    free(viewer.source_path); viewer.source_path=NULL; viewer.image_count=0;
}

static void fb_activate_selected(void) {
    if (fb.entry_count==0) return;
    const char *name = fb.entries[fb.selected];
    if (strcmp(name, "..") == 0) {
        char *slash = strrchr(fb.current_path, '/');
        if (slash && slash != fb.current_path) *slash='\0';
        else if (slash && slash==fb.current_path) fb.current_path[1]='\0';
        fb_scan(); return;
    }
    char full[PATH_MAX]; snprintf(full,sizeof(full),"%s/%s", fb.current_path, name);
    struct stat st; if (stat(full,&st)!=0) return;
    if (S_ISDIR(st.st_mode)) { strncpy(fb.current_path, full, sizeof(fb.current_path)-1); fb.current_path[sizeof(fb.current_path)-1]='\0'; fb_scan(); }
    else if (fb_is_supported(name)) {
        fb_unload_current_comic();
        if (comic_viewer_load(full)) {
            if (viewer_has_current_view()) viewer_init_view(viewer.current_view_node);
        }
        file_browser_close();
    }
}

void file_browser_handle_key(SDL_Keycode key) {
    if (!fb.active) return;
    switch (key) {
        case SDLK_ESCAPE: file_browser_close(); break;
        case SDLK_UP: if (fb.selected>0){ fb.selected--; if(fb.selected<fb.scroll_offset) fb.scroll_offset=fb.selected; } break;
        case SDLK_DOWN: if (fb.selected<fb.entry_count-1){ fb.selected++; int bottom=fb.scroll_offset+fb.max_visible-1; if (fb.selected>bottom) fb.scroll_offset++; } break;
        case SDLK_PAGEUP: fb.selected-=fb.max_visible; if (fb.selected<0) fb.selected=0; if (fb.selected<fb.scroll_offset) fb.scroll_offset=fb.selected; break;
        case SDLK_PAGEDOWN: fb.selected+=fb.max_visible; if (fb.selected>=fb.entry_count) fb.selected=fb.entry_count-1; while (fb.selected>=fb.scroll_offset+fb.max_visible) fb.scroll_offset++; break;
        case SDLK_RETURN: case SDLK_KP_ENTER: fb_activate_selected(); break;
        default: break;
    }
}

void file_browser_render(void) {
    if (!fb.active) return;
    SDL_SetRenderDrawBlendMode(viewer.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(viewer.renderer, 0,0,0,200);
    SDL_FRect panel = {40,40, viewer.window_width-80.0f, viewer.window_height-80.0f};
    SDL_RenderFillRect(viewer.renderer, &panel);
    int start=fb.scroll_offset; int end=start+fb.max_visible; if (end>fb.entry_count) end=fb.entry_count; float y=panel.y+10;
    for (int i=start;i<end;i++) {
        char line[PATH_MAX+10]; snprintf(line,sizeof(line),"%c %s%s", (i==fb.selected?'>':' '), fb.entries[i], fb.is_dir[i]?"/":"");
        SDL_Color color = {220,220,220,255}; if (fb.is_dir[i]) { color.r=180; color.g=200; color.b=255; } if (i==fb.selected){ color.r=255; color.g=255; color.b=0; }
    SDL_Texture *txt = viewer_render_text(line, color);
        if (txt){ float tw, th; SDL_GetTextureSize(txt,&tw,&th); SDL_FRect r={panel.x+10,y,tw,th}; SDL_RenderTexture(viewer.renderer, txt,NULL,&r); SDL_DestroyTexture(txt); y+=th+4; }
    }
    char path_line[PATH_MAX+32]; snprintf(path_line,sizeof(path_line),"Path: %s", fb.current_path);
    SDL_Texture *pth = viewer_render_text(path_line,(SDL_Color){180,180,180,255});
    if (pth){ float tw, th; SDL_GetTextureSize(pth,&tw,&th); SDL_FRect r={panel.x+10, panel.y+panel.h-th-10, tw, th}; SDL_RenderTexture(viewer.renderer,pth,NULL,&r); SDL_DestroyTexture(pth);}    
}
