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
static void draw_cropping_rect(SDL_FRect rect) {
    // Semi-transparent darken outside area and stroke rectangle
    SDL_Color border = {255, 255, 255, 220};
    SDL_SetRenderDrawColor(viewer.renderer, 0, 0, 0, 120);
    // darken outside by drawing four rects around selection
    SDL_FRect top = {0, 0, (float)viewer.drawable_width, rect.y};
    SDL_FRect left = {0, rect.y, rect.x, rect.h};
    SDL_FRect right = {rect.x + rect.w, rect.y, (float)viewer.drawable_width - (rect.x + rect.w), rect.h};
    SDL_FRect bottom = {0, rect.y + rect.h, (float)viewer.drawable_width, (float)viewer.drawable_height - (rect.y + rect.h)};
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
    float rx = fminf(x1, x2);
    float ry = fminf(y1, y2);
    float rw = fabsf(x2 - x1);
    float rh = fabsf(y2 - y1);
    // Clamp to drawable
    if (rx < 0) rx = 0;
    if (ry < 0) ry = 0;
    if (rx + rw > viewer.drawable_width) rw = viewer.drawable_width - rx;
    if (ry + rh > viewer.drawable_height) rh = viewer.drawable_height - ry;
    SDL_FRect rect = { rx, ry, rw, rh };
    draw_cropping_rect(rect);

    // Show hint text
    SDL_Texture *hint = viewer_render_text("Middle drag to select crop. Enter=apply, Esc=cancel", (SDL_Color){255,255,255,255});
    if (hint) {
        float w, h;
        SDL_GetTextureSize(hint, &w, &h);
        SDL_FRect dst = { 10.0f, (float)viewer.drawable_height - h - 10.0f, w, h };
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

        char info_text[128];
        int w = 0, h = 0;
        if (viewer.current_view_node && viewer.current_view_node->texture) {
            w = (int)viewer.current_view_node->texture->w;
            h = (int)viewer.current_view_node->texture->h;
        }

        snprintf(info_text, sizeof(info_text), "%d / %d %s [%s] (%dx%d)",
        viewer.current_view_index + 1, view_count,
        options ? (options->enhancement_enabled ? "[E+]" : "[E-]") : "[E-]",
        overlay_str, w, h);

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
                SDL_FRect dst = { (float)viewer.drawable_width - w - 10.0f, 10.0f, w, h };
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
        viewer_display_info();
        file_browser_render();

        imgui_layer_render();
        SDL_RenderPresent(viewer.renderer);
        return;
    }

    float display_area_width = (float)viewer.drawable_width;
    float display_area_height = (float)viewer.drawable_height;

    float overall_content_start_x = display_area_width;
    float overall_content_end_x = 0.0f;

    // Use crop_rect dimensions as the effective source size to preserve aspect ratio.
    // When crop_rect is set (non-zero), it defines the actual visible region which may
    // differ from the full texture dimensions (e.g. after auto-crop trims white borders).
    SDL_FRect *src_rect = NULL;
    float src_w, src_h;
    if (current_display_view->crop_rect.w > 0 && current_display_view->crop_rect.h > 0) {
        src_rect = &current_display_view->crop_rect;
        src_w = current_display_view->crop_rect.w;
        src_h = current_display_view->crop_rect.h;
    } else {
        src_w = (float)current_display_view->texture->w;
        src_h = (float)current_display_view->texture->h;
    }

    float scale_height = display_area_height / src_h;
    float scale_width = display_area_width / src_w;
    float scale = fminf(scale_height, scale_width);

    float final_scale = scale;
    if (viewer.zoomed) final_scale = scale * viewer.zoom_level;

    if (final_scale <= 1e-6f) final_scale = 1e-6f;

    int scaled_width = (int)(src_w * final_scale);
    int scaled_height = (int)(src_h * final_scale);
    if (scaled_width <= 0) scaled_width = 1;
    if (scaled_height <= 0) scaled_height = 1;

    float start_x = (display_area_width - (src_w * scale)) / 2.0f;

    float x_pos_render, y_pos_render;
    if (viewer.zoomed) {
        float unzoomed_width = src_w * scale;
        float unzoomed_height = src_h * scale;
        float unzoomed_x = (display_area_width - unzoomed_width) / 2.0f;
        float unzoomed_y = (display_area_height - unzoomed_height) / 2.0f;

        float cursor_relative_x = viewer.zoom_center_x - unzoomed_x;
        float cursor_relative_y = viewer.zoom_center_y - unzoomed_y;

        float zoomed_point_x = cursor_relative_x * viewer.zoom_level;
        float zoomed_point_y = cursor_relative_y * viewer.zoom_level;

        x_pos_render = viewer.zoom_center_x - zoomed_point_x;
        y_pos_render = viewer.zoom_center_y - zoomed_point_y;

        if (scaled_height > display_area_height) {
            float max_pan_y = -y_pos_render;
            float min_pan_y = display_area_height - scaled_height - y_pos_render;
            if (viewer.pan_offset_y > max_pan_y) viewer.pan_offset_y = max_pan_y;
            if (viewer.pan_offset_y < min_pan_y) viewer.pan_offset_y = min_pan_y;
        } else {
            viewer.pan_offset_y = 0;
        }

        x_pos_render += viewer.pan_offset_x;
        y_pos_render += viewer.pan_offset_y;
    } else {
        x_pos_render = start_x;
        y_pos_render = (display_area_height - scaled_height) / 2.0f;
    }

    if (x_pos_render < overall_content_start_x) overall_content_start_x = x_pos_render;
    if (x_pos_render + scaled_width > overall_content_end_x) overall_content_end_x = x_pos_render + scaled_width;

    // Render overlay using the new module
    render_overlay(viewer.renderer, current_display_view, 
                  display_area_width, display_area_height, 
                  overall_content_start_x, overall_content_end_x);

    // Now render the main image on top
    SDL_FRect dest_rect = { x_pos_render, y_pos_render, (float)scaled_width, (float)scaled_height };

    // Apply the user-selected scale mode
    SDL_SetTextureScaleMode(current_display_view->texture, viewer.scale_mode);

    SDL_RenderTexture(viewer.renderer, current_display_view->texture, src_rect, &dest_rect);

    // Draw cropping overlay before info/file browser so guides are visible under UI
    viewer_render_cropping_overlay();
    viewer_display_info();
    file_browser_render();

    imgui_layer_render();
    SDL_RenderPresent(viewer.renderer);
}
