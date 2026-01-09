/* overlay.c - Overlay rendering implementation */

#include "overlay.h"
#include "comic_viewer.h"

extern struct ViewerState viewer;

// Extracted from render.c or edges.c presumably, but render.c was calling it.
// We need to declare it here or include a header that has it. 
// Assuming it is in edges.h based on previous context, but let's check imports.
#include "edges.h" 

// Helper function definitions

static SDL_Color compute_average_color(SDL_Surface *surface, int x, int y, int w, int h) {
    long r_sum = 0, g_sum = 0, b_sum = 0;
    int count = 0;
    
    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(surface->format);
    int bpp = fmt->bytes_per_pixel;
    uint8_t *pixels = (uint8_t *)surface->pixels;
    int pitch = surface->pitch;
    SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    for (int j = y; j < y + h; j++) {
        // Clamp y
        if (j < 0 || j >= surface->h) continue;
        
        for (int i = x; i < x + w; i++) {
            // Clamp x
            if (i < 0 || i >= surface->w) continue;

            uint8_t *p = pixels + j * pitch + i * bpp;
            uint32_t pixel = 0;
            
            switch (bpp) {
                case 1: pixel = *p; break;
                case 2: pixel = *(uint16_t *)p; break;
                case 3: 
                    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        pixel = p[0] << 16 | p[1] << 8 | p[2]; 
                    #else
                        pixel = p[0] | p[1] << 8 | p[2] << 16; 
                    #endif
                    break;
                case 4: pixel = *(uint32_t *)p; break;
            }

            uint8_t r, g, b, a;
            SDL_GetRGBA(pixel, fmt, palette, &r, &g, &b, &a);
            
            r_sum += r;
            g_sum += g;
            b_sum += b;
            count++;
        }
    }

    SDL_Color result = {0, 0, 0, 255};
    if (count > 0) {
        result.r = r_sum / count;
        result.g = g_sum / count;
        result.b = b_sum / count;
    }
    return result;
}

static void render_vertical_gradient(SDL_Renderer *renderer, SDL_FRect rect, SDL_Color top_c, SDL_Color bottom_c) {
    float alpha = 100.0f / 255.0f;
    
    SDL_Vertex vertices[4] = {
        {{rect.x, rect.y}, {top_c.r/255.0f, top_c.g/255.0f, top_c.b/255.0f, alpha}, {0.0f, 0.0f}},
        {{rect.x + rect.w, rect.y}, {top_c.r/255.0f, top_c.g/255.0f, top_c.b/255.0f, alpha}, {1.0f, 0.0f}},
        {{rect.x + rect.w, rect.y + rect.h}, {bottom_c.r/255.0f, bottom_c.g/255.0f, bottom_c.b/255.0f, alpha}, {1.0f, 1.0f}},
        {{rect.x, rect.y + rect.h}, {bottom_c.r/255.0f, bottom_c.g/255.0f, bottom_c.b/255.0f, alpha}, {0.0f, 1.0f}}
    };

    int indices[] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, NULL, vertices, 4, indices, 6);
}

void render_overlay(SDL_Renderer *renderer, ImageView *view, 
                   float display_width, float display_height,
                   float content_start_x, float content_end_x) {
    
    if (viewer.overlay_mode == OVERLAY_STRETCHED) {
        // Full width overlay with opacity - render background FIRST
        SDL_BlendMode old_mode;
        SDL_GetTextureBlendMode(view->texture, &old_mode);
        SDL_SetTextureBlendMode(view->texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(view->texture, 40); // ~15% opacity

        // Calculate scale to fit width
        float bg_scale = display_width / view->texture->w;
        float bg_height = view->texture->h * bg_scale;
        float bg_y = (display_height - bg_height) / 2.0f;

        // Draw the texture centered vertically, full width
        SDL_FRect bg_rect = {0, bg_y, display_width, bg_height};
        SDL_RenderTexture(renderer, view->texture, &view->crop_rect, &bg_rect);

        SDL_SetTextureAlphaMod(view->texture, 255); // Restore alpha
        SDL_SetTextureBlendMode(view->texture, old_mode); // Restore blend mode
    } else if (viewer.overlay_mode == OVERLAY_AMBILIGHT) {
        // Ambilight effect: 24 averaged colors per side
        
        if (view->surface) {
            int samples = 24;
            float sample_height = view->crop_rect.h / (float)samples;
            float render_step_y = display_height / (float)samples;
            
            SDL_Color colors[24];

            // Left side
            if (content_start_x > 0) {
                // Compute colors
                for (int i = 0; i < samples; i++) {
                    int y_start = (int)(view->crop_rect.y + i * sample_height);
                    int h = (int)sample_height;
                    if (h < 1) h = 1;
                    colors[i] = compute_average_color(view->surface, (int)view->crop_rect.x, y_start, 5, h);
                }

                // Render gradients
                SDL_FRect rect = {0, 0, content_start_x, render_step_y / 2.0f};
                render_vertical_gradient(renderer, rect, colors[0], colors[0]);

                for (int i = 0; i < samples - 1; i++) {
                    float y = (i * render_step_y) + (render_step_y / 2.0f);
                    rect.y = y;
                    rect.h = render_step_y;
                    render_vertical_gradient(renderer, rect, colors[i], colors[i+1]);
                }

                float last_y = ((samples - 1) * render_step_y) + (render_step_y / 2.0f);
                rect.y = last_y;
                rect.h = display_height - last_y;
                render_vertical_gradient(renderer, rect, colors[samples-1], colors[samples-1]);
            }

            // Right side
            if (content_end_x < display_width) {
                // Compute colors
                for (int i = 0; i < samples; i++) {
                    int y_start = (int)(view->crop_rect.y + i * sample_height);
                    int h = (int)sample_height;
                    if (h < 1) h = 1;
                    colors[i] = compute_average_color(view->surface, (int)(view->crop_rect.x + view->crop_rect.w - 5), y_start, 5, h);
                }

                // Render gradients
                SDL_FRect rect = {content_end_x, 0, display_width - content_end_x, render_step_y / 2.0f};
                render_vertical_gradient(renderer, rect, colors[0], colors[0]);

                for (int i = 0; i < samples - 1; i++) {
                    float y = (i * render_step_y) + (render_step_y / 2.0f);
                    rect.y = y;
                    rect.h = render_step_y;
                    render_vertical_gradient(renderer, rect, colors[i], colors[i+1]);
                }

                float last_y = ((samples - 1) * render_step_y) + (render_step_y / 2.0f);
                rect.y = last_y;
                rect.h = display_height - last_y;
                render_vertical_gradient(renderer, rect, colors[samples-1], colors[samples-1]);
            }
        }
    } else {
        // Default to OVERLAY_GRADIENT
        SDL_Rect left_rect_gradient = {0, 0, (int)content_start_x, (int)display_height};
        if (left_rect_gradient.w > 0) {
            render_horizontal_gradient_hsl(renderer, left_rect_gradient, view->left_edge_color, false);
        }

        SDL_Rect right_rect_gradient = {(int)content_end_x, 0, (int)(display_width - content_end_x), (int)display_height};
        if (right_rect_gradient.w > 0) {
            render_horizontal_gradient_hsl(renderer, right_rect_gradient, view->right_edge_color, true);
        }
    }
}
