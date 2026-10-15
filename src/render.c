/* render.c - rendering implementation extracted from comic_viewer.c */

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include "comic_viewer.h"
#include "render.h"
#include "image_processor.h"
#include "progress_indicator.h"
#include "file_browser.h"
#include "overlay.h"
#include "imgui_layer.h"

extern struct ViewerState viewer;
extern ImageProcessingOptions* options;

bool viewer_get_view_rects(const ImageView *view, SDL_FRect *source, SDL_FRect *destination) {
    if (!view || !view->texture || !source || !destination ||
        viewer.window_width <= 0 || viewer.window_height <= 0) return false;
    *source = view->crop_rect;
    if (source->w <= 0 || source->h <= 0) {
        *source = (SDL_FRect){0, 0, view->texture->w, view->texture->h};
    }
    if (source->w <= 0 || source->h <= 0) return false;
    float scale = fminf(viewer.window_width / source->w, viewer.window_height / source->h);
    float base_width = source->w * scale;
    float base_height = source->h * scale;
    float base_x = (viewer.window_width - base_width) / 2.0f;
    float base_y = (viewer.window_height - base_height) / 2.0f;
    *destination = (SDL_FRect){base_x, base_y, base_width, base_height};
    if (viewer.zoomed) {
        float zoom = fmaxf(viewer.zoom_level, 1e-6f);
        destination->w *= zoom;
        destination->h *= zoom;
        destination->x = viewer.zoom_center_x - (viewer.zoom_center_x - base_x) * zoom;
        destination->y = viewer.zoom_center_y - (viewer.zoom_center_y - base_y) * zoom;
        if (destination->h > viewer.window_height) {
            viewer.pan_offset_y = fmaxf(viewer.window_height - destination->h - destination->y,
                                       fminf(-destination->y, viewer.pan_offset_y));
        } else {
            viewer.pan_offset_y = 0;
        }
        destination->x += viewer.pan_offset_x;
        destination->y += viewer.pan_offset_y;
    }
    return true;
}

bool viewer_apply_crop_selection(void) {
    SDL_FRect source, destination;
    if (!viewer_get_view_rects(viewer.current_view_node, &source, &destination)) return false;
    float left = fmaxf(0, fmaxf(destination.x, fminf(viewer.crop_start_x, viewer.crop_current_x)));
    float top = fmaxf(0, fmaxf(destination.y, fminf(viewer.crop_start_y, viewer.crop_current_y)));
    float right = fminf(viewer.window_width,
                       fminf(destination.x + destination.w, fmaxf(viewer.crop_start_x, viewer.crop_current_x)));
    float bottom = fminf(viewer.window_height,
                        fminf(destination.y + destination.h, fmaxf(viewer.crop_start_y, viewer.crop_current_y)));
    if (right - left <= 1 || bottom - top <= 1) return false;
    viewer.current_view_node->crop_rect = (SDL_FRect){
        source.x + (left - destination.x) * source.w / destination.w,
        source.y + (top - destination.y) * source.h / destination.h,
        (right - left) * source.w / destination.w,
        (bottom - top) * source.h / destination.h
    };
    return true;
}

static void draw_cropping_rect(SDL_FRect rect) {
    // Semi-transparent darken outside area and stroke rectangle
    SDL_Color border = {255, 255, 255, 220};
    SDL_SetRenderDrawColor(viewer.renderer, 0, 0, 0, 120);
    // darken outside by drawing four rects around selection
    SDL_FRect top = {0, 0, (float)viewer.window_width, rect.y};
    SDL_FRect left = {0, rect.y, rect.x, rect.h};
    SDL_FRect right = {rect.x + rect.w, rect.y, (float)viewer.window_width - (rect.x + rect.w), rect.h};
    SDL_FRect bottom = {0, rect.y + rect.h, (float)viewer.window_width, (float)viewer.window_height - (rect.y + rect.h)};
    SDL_RenderFillRect(viewer.renderer, &top);
    SDL_RenderFillRect(viewer.renderer, &left);
    SDL_RenderFillRect(viewer.renderer, &right);
    SDL_RenderFillRect(viewer.renderer, &bottom);

    SDL_SetRenderDrawColor(viewer.renderer, border.r, border.g, border.b, border.a);
    // Draw border lines (approximate by thin rects)
    SDL_FRect t = {rect.x, rect.y, rect.w, 1.5f};
    SDL_FRect b = {rect.x, rect.y + rect.h - 1.5f, rect.w, 1.5f};
    SDL_FRect l = {rect.x, rect.y, 1.5f, rect.h};
    SDL_FRect r = {rect.x + rect.w - 1.5f, rect.y, 1.5f, rect.h};
    SDL_RenderFillRect(viewer.renderer, &t);
    SDL_RenderFillRect(viewer.renderer, &b);
    SDL_RenderFillRect(viewer.renderer, &l);
    SDL_RenderFillRect(viewer.renderer, &r);
}

void viewer_render_cropping_overlay(void) {
    if (!viewer.cropping_mode) return;
    // Build normalized rect from start and current points
    float x1 = viewer.crop_start_x;
    float y1 = viewer.crop_start_y;
    float x2 = viewer.crop_current_x;
    float y2 = viewer.crop_current_y;
    float rx = fminf(viewer.window_width, fmaxf(0, fminf(x1, x2)));
    float ry = fminf(viewer.window_height, fmaxf(0, fminf(y1, y2)));
    float rw = fmaxf(0, fminf(viewer.window_width, fmaxf(x1, x2)) - rx);
    float rh = fmaxf(0, fminf(viewer.window_height, fmaxf(y1, y2)) - ry);
    SDL_FRect rect = { rx, ry, rw, rh };
    draw_cropping_rect(rect);

    // Show hint text
    SDL_Texture *hint = viewer_render_text("Middle drag to select crop. Enter=apply, Esc=cancel", (SDL_Color){255,255,255,255});
    if (hint) {
        float w, h;
        SDL_GetTextureSize(hint, &w, &h);
        SDL_FRect dst = { 10.0f, (float)viewer.window_height - h - 10.0f, w, h };
        SDL_RenderTexture(viewer.renderer, hint, NULL, &dst);
        SDL_DestroyTexture(hint);
    }
}

static SDL_Texture* render_text_internal(const char *text, SDL_Color color) {
    if (!viewer.font || !text) return NULL;

    SDL_Surface *surface = TTF_RenderText_Blended(viewer.font, text, 0, color);
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to render text: %s", SDL_GetError());
        return NULL;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(viewer.renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture from text: %s", SDL_GetError());
    }

    return texture;
}

SDL_Texture* viewer_render_text(const char *text, SDL_Color color) { return render_text_internal(text, color); }

void viewer_display_info(void) {
    if (viewer.image_count <= 1) return;

    Uint64 current_time = SDL_GetTicks();
    Uint64 elapsed_time = current_time - viewer.last_page_change_time;
    if (elapsed_time <= 2000) {
        int view_count = viewer.view_count;
        float progress = 0.0f;
        if (view_count > 1) progress = (float)viewer.current_view_index / (float)(view_count - 1);
        int radius = 40;
        int centerX = 100;
        int centerY = 50;
        draw_progress_indicator(viewer.renderer, progress, centerX, centerY, radius);

        const char* overlay_str = "GRAD";
        if (viewer.overlay_mode == OVERLAY_STRETCHED) overlay_str = "STR";
        else if (viewer.overlay_mode == OVERLAY_AMBILIGHT) overlay_str = "AMBI";
        else if (viewer.overlay_mode == OVERLAY_PIXEL_SHADER) overlay_str = "SHADER";

        const char* filter_str = "";
        if (options) {
            switch (options->color_filter) {
                case COLOR_FILTER_NONE: filter_str = ""; break;
                case COLOR_FILTER_GRAY_WORLD: filter_str = "[GW]"; break;
                case COLOR_FILTER_WARM_NEUTRALIZER: filter_str = "[WN]"; break;
                case COLOR_FILTER_COOL_NEUTRALIZER: filter_str = "[CN]"; break;
                case COLOR_FILTER_BOOST_CONTRAST: filter_str = "[BC]"; break;
                case COLOR_FILTER_DESATURATE: filter_str = "[DES]"; break;
                case COLOR_FILTER_BINARIZE: filter_str = "[BW]"; break;
                default: filter_str = ""; break;
            }
        }

        char info_text[128];
        int w = 0, h = 0;
        if (viewer.current_view_node && viewer.current_view_node->texture) {
            w = (int)viewer.current_view_node->texture->w;
            h = (int)viewer.current_view_node->texture->h;
        }

        char overlay_label[48];
        if (viewer.overlay_mode == OVERLAY_PIXEL_SHADER) {
            snprintf(overlay_label, sizeof(overlay_label), "%s:%s", overlay_str,
                     pixel_shader_get_name(viewer.overlay_pixel_shader));
        } else {
            snprintf(overlay_label, sizeof(overlay_label), "%s", overlay_str);
        }

        char view_shader_buf[32] = "";
        if (viewer.view_pixel_shader != PIXEL_SHADER_NONE) {
            snprintf(view_shader_buf, sizeof(view_shader_buf), "[PS:%s]",
                     pixel_shader_get_name(viewer.view_pixel_shader));
        }

        snprintf(info_text, sizeof(info_text), "%d / %d %s %s %s %s [%s] (%dx%d)",
        viewer.current_view_index + 1, view_count,
        options ? (options->enhancement_enabled ? "[E+]" : "[E-]") : "[E-]",
        filter_str,
        viewer.border_removal_enabled ? "[BR+]" : "[BR-]",
        view_shader_buf,
        overlay_label, w, h);

        SDL_Texture *text_texture = render_text_internal(info_text, (SDL_Color){255,255,255,255});
        if (text_texture) {
            float text_width, text_height;
            SDL_GetTextureSize(text_texture, &text_width, &text_height);
            float text_x = (float)(centerX - text_width / 2);
            if (text_x < 10) text_x = 10;
            SDL_FRect text_rect = { text_x, (float)(centerY + radius + 10), text_width, text_height };
            SDL_RenderTexture(viewer.renderer, text_texture, NULL, &text_rect);
            SDL_DestroyTexture(text_texture);
        }
    }

    // Show transient gamepad status messages (if set and not expired)
    if (viewer.gamepad_status_msg[0] != '\0') {
        Uint64 now = SDL_GetTicks();
        if (now <= viewer.gamepad_status_until) {
            SDL_Texture *msg = render_text_internal(viewer.gamepad_status_msg, (SDL_Color){255,255,0,255});
            if (msg) {
                float w, h;
                SDL_GetTextureSize(msg, &w, &h);
                SDL_FRect dst = { (float)viewer.window_width - w - 10.0f, 10.0f, w, h };
                SDL_RenderTexture(viewer.renderer, msg, NULL, &dst);
                SDL_DestroyTexture(msg);
            }
        } else {
            // clear message once expired
            viewer.gamepad_status_msg[0] = '\0';
        }
    }

}

void viewer_render_current_view(void) {
    // full render implementation based on previous code in comic_viewer.c
    SDL_SetRenderDrawColor(viewer.renderer, 30, 30, 30, 255);
    SDL_RenderClear(viewer.renderer);

    imgui_layer_new_frame();

    ImageView *current_display_view = viewer.current_view_node;
    if (!current_display_view || !current_display_view->texture) {
        // Page is loading or empty - show loading indicator
        if (current_display_view && viewer.load_thread) {
            // Page is actively loading - show spinner
            int centerX = (int)((float)viewer.window_width / 2.0f);
            int centerY = (int)((float)viewer.window_height / 2.0f);
            int radius = 30;
            
            // Animate the spinner based on time
            Uint64 elapsed = SDL_GetTicks() - viewer.last_page_change_time;
            float progress = fmodf((float)elapsed / 800.0f, 1.0f); // Full rotation every 800ms
            draw_progress_indicator(viewer.renderer, progress, centerX, centerY, radius);
            
            // Show "Loading..." text below the spinner
            char loading_message[64];
            snprintf(loading_message, sizeof(loading_message), "Loading page %d...", viewer.current_view_index + 1);
            SDL_Texture *loading_text = render_text_internal(loading_message, (SDL_Color){200,200,200,255});
            if (loading_text) {
                float w, h;
                SDL_GetTextureSize(loading_text, &w, &h);
                SDL_FRect text_rect = { 
                    (float)centerX - w / 2.0f, 
                    (float)centerY + radius + 20.0f, 
                    w, h 
                };
                SDL_RenderTexture(viewer.renderer, loading_text, NULL, &text_rect);
                SDL_DestroyTexture(loading_text);
            }
        }
        
        viewer_display_info();
        file_browser_render();

        imgui_layer_render();
        SDL_RenderPresent(viewer.renderer);
        return;
    }

    float display_area_width = (float)viewer.window_width;
    float display_area_height = (float)viewer.window_height;
    SDL_FRect source, destination;
    if (!viewer_get_view_rects(current_display_view, &source, &destination)) return;

    // Render overlay using the new module
    render_overlay(viewer.renderer, current_display_view, 
                  display_area_width, display_area_height, 
                  destination.x, destination.x + destination.w);

    // Now render the main image on top
    // Apply the user-selected scale mode
    SDL_SetTextureScaleMode(current_display_view->texture, viewer.scale_mode);

    SDL_RenderTexture(viewer.renderer, current_display_view->texture, &source, &destination);

    // Draw cropping overlay before info/file browser so guides are visible under UI
    viewer_render_cropping_overlay();
    viewer_display_info();
    file_browser_render();

    imgui_layer_render();
    SDL_RenderPresent(viewer.renderer);
}

