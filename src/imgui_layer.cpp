#include "imgui_layer.h"
#include "i18n.h"

#include <cstdio>
#include <cstring>
#include <SDL3/SDL.h>

#include "comic_viewer.h"
#include "file_browser.h"
#include "image_loader.h"
#include "image_processor.h"
#include "pdf_backend.h"
#include "pixel_shader.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

static bool g_imgui_initialized = false;
static bool g_imgui_visible = false;
static bool g_exif_visible = false;
static SDL_Window *g_window = nullptr;
static SDL_Renderer *g_renderer = nullptr;

// Cache of the metadata read for the currently displayed image so we do not
// re-read the file every frame. Refreshed when the displayed path changes.
static ImageExifEntry g_exif_entries[IMAGE_EXIF_MAX_ENTRIES];
static int g_exif_count = 0;
static char g_exif_path[4096] = {0};
static char g_status_message[256] = {0};
static Uint64 g_status_message_time = 0;   // Timestamp when the message was set
static Uint64 g_status_message_duration = 5000; // Auto-dismiss after 5 seconds

extern "C" {
extern ImageProcessingOptions* options;
}

bool imgui_layer_init(SDL_Window *window, SDL_Renderer *renderer) {
    if (g_imgui_initialized) return true;
    if (!window || !renderer) return false;

    g_window = window;
    g_renderer = renderer;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(g_window, g_renderer)) {
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplSDLRenderer3_Init(g_renderer)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    g_imgui_initialized = true;
    return true;
}

void imgui_layer_shutdown(void) {
    if (!g_imgui_initialized) return;

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    g_imgui_initialized = false;
    g_imgui_visible = false;
    g_window = nullptr;
    g_renderer = nullptr;
}

bool imgui_layer_is_initialized(void) {
    return g_imgui_initialized;
}

void imgui_layer_process_event(const SDL_Event *event) {
    if (!g_imgui_initialized || !event) return;
    ImGui_ImplSDL3_ProcessEvent(event);
}

void imgui_layer_new_frame(void) {
    if (!g_imgui_initialized) return;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

// Index into viewer.images of the primary image in the current view, or -1.
static int current_image_index(void) {
    if (!viewer.current_view_node || viewer.current_view_node->count <= 0) {
        return -1;
    }
    int idx = viewer.current_view_node->image_indices[0];
    if (idx < 0 || idx >= viewer.image_count) {
        return -1;
    }
    return idx;
}

// Re-read metadata only when the displayed image path changes.
static void exif_refresh(const char *path) {
    if (!path) {
        g_exif_path[0] = '\0';
        g_exif_count = 0;
        return;
    }
    if (strncmp(g_exif_path, path, sizeof(g_exif_path)) == 0) {
        return; // Already cached for this path.
    }
    strncpy(g_exif_path, path, sizeof(g_exif_path) - 1);
    g_exif_path[sizeof(g_exif_path) - 1] = '\0';
    g_exif_count = image_load_exif(path, g_exif_entries, IMAGE_EXIF_MAX_ENTRIES);
}

static void imgui_layer_build_exif_ui(void) {
    if (!g_exif_visible) return;

    int idx = current_image_index();
    const char *path = (idx >= 0) ? viewer.images[idx].path : nullptr;
    exif_refresh(path);

    ImGui::SetNextWindowSize(ImVec2(480.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(IC_("Image Info (I to toggle)"), &g_exif_visible)) {
        if (idx < 0 || !path) {
            ImGui::TextUnformatted("No image is currently displayed.");
            ImGui::End();
            return;
        }

        const char *name = strrchr(path, '/');
        ImGui::SeparatorText("File");
        ImGui::TextWrapped("%s", name ? name + 1 : path);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", path);
        }

        SDL_Surface *surface = viewer.images[idx].surface;
        if (surface) {
            ImGui::Text("Dimensions: %d x %d px", surface->w, surface->h);
            const char *fmt = SDL_GetPixelFormatName(surface->format);
            if (fmt) {
                ImGui::Text("Pixel format: %s", fmt);
            }
        }

        ImGui::SeparatorText("EXIF Metadata");
        if (!image_loader_exif_available()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               "Metadata extraction requires the FreeImage backend.");
        } else if (g_exif_count == 0) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               "No EXIF metadata found in this image.");
        } else if (ImGui::BeginTable("exif_table", 2,
                                     ImGuiTableFlags_Borders |
                                     ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (int i = 0; i < g_exif_count; i++) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(g_exif_entries[i].key);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", g_exif_entries[i].value);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

static void imgui_layer_build_ui(void) {
    // Always render the status message overlay if set (regardless of visibility toggle)
    if (g_status_message[0] != '\0') {
        // Auto-dismiss after duration
        Uint64 elapsed = SDL_GetTicks() - g_status_message_time;
        float alpha = 1.0f;
        if (elapsed > g_status_message_duration) {
            g_status_message[0] = '\0';
        } else {
            // Fade out during the last second
            if (elapsed > g_status_message_duration - 1000) {
                alpha = (float)(g_status_message_duration - elapsed) / 1000.0f;
            }

            ImGuiIO &io = ImGui::GetIO();
            ImVec2 display_size = io.DisplaySize;

            // Center the window on screen
            ImGui::SetNextWindowPos(ImVec2(display_size.x * 0.5f, display_size.y * 0.5f),
                                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f)); // Auto-size

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav |
                                     ImGuiWindowFlags_NoInputs;

            // Semi-transparent background with fade
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30.0f, 20.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 0.9f));

            if (ImGui::Begin("##StatusMessage", nullptr, flags)) {
                // Icon-like prefix
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
                ImGui::TextUnformatted("!");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextUnformatted(g_status_message);
            }
            ImGui::End();

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }
    }

    // The file browser owns the whole screen and the whole input focus while it
    // is open: suppress the viewer's own panels so none of their controls can
    // be clicked underneath it. Their visibility flags are left untouched, so
    // whatever was open reappears once the browser closes. The status message
    // above is exempt - it is click-through and is what explains why the
    // browser opened in the first place.
    if (file_browser_is_active()) return;

    // The image-info overlay is independent from the F1 settings window.
    imgui_layer_build_exif_ui();

    if (!g_imgui_visible) return;

    ImGui::SetNextWindowSize(ImVec2(460.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(IC_("IC Settings (F1 to toggle)"), &g_imgui_visible)) {
        ImGui::Text("Renderer: %s", SDL_GetRendererName(g_renderer));

        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(g_window, &win_w, &win_h);
        ImGui::Text("Window: %dx%d", win_w, win_h);

        int pix_w = 0, pix_h = 0;
        SDL_GetWindowSizeInPixels(g_window, &pix_w, &pix_h);
        ImGui::Text("Drawable: %dx%d", pix_w, pix_h);

        ImGui::Spacing();

        ImGui::SeparatorText("Visual");
        {
            static const char *overlay_items[] = {
                "Gradient", "Stretched", "Ambilight", "Pixel Shader"
            };
            int overlay_index = (int)viewer.overlay_mode;
            if (overlay_index < 0) overlay_index = 0;
            if (overlay_index >= (int)OVERLAY_MODE_COUNT) overlay_index = (int)OVERLAY_MODE_COUNT - 1;

            if (ImGui::Combo("Overlay Mode", &overlay_index, overlay_items, IM_ARRAYSIZE(overlay_items))) {
                viewer.overlay_mode = (OverlayMode)overlay_index;
                viewer.last_page_change_time = SDL_GetTicks();
            }

            // Build a shared shader-name list once per frame.
            const int shader_count = pixel_shader_count();
            static const char *shader_items[PIXEL_SHADER_COUNT];
            for (int i = 0; i < shader_count && i < PIXEL_SHADER_COUNT; ++i) {
                shader_items[i] = pixel_shader_get_name((PixelShaderType)i);
            }

            // Overlay shader: only meaningful in Pixel Shader overlay mode.
            ImGui::BeginDisabled(viewer.overlay_mode != OVERLAY_PIXEL_SHADER);
            int overlay_shader_index = (int)viewer.overlay_pixel_shader;
            if (overlay_shader_index < 0) overlay_shader_index = 0;
            if (overlay_shader_index >= shader_count) overlay_shader_index = shader_count - 1;
            if (ImGui::Combo("Overlay Shader", &overlay_shader_index,
                             shader_items, shader_count)) {
                viewer.overlay_pixel_shader = (PixelShaderType)overlay_shader_index;
                viewer.last_page_change_time = SDL_GetTicks();
                char msg[96];
                snprintf(msg, sizeof(msg), "Overlay shader: %s",
                         pixel_shader_get_name(viewer.overlay_pixel_shader));
                imgui_layer_set_status_message(msg);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Active only when Overlay Mode is set to Pixel Shader (cycle with Shift+O)");
            ImGui::EndDisabled();

            // View shader: applied to the loaded surface, requires reloading the view.
            int view_shader_index = (int)viewer.view_pixel_shader;
            if (view_shader_index < 0) view_shader_index = 0;
            if (view_shader_index >= shader_count) view_shader_index = shader_count - 1;
            if (ImGui::Combo("View Shader", &view_shader_index,
                             shader_items, shader_count)) {
                comic_viewer_finish_loading();
                viewer.view_pixel_shader = (PixelShaderType)view_shader_index;
                comic_viewer_reload_current_view();
                viewer.last_page_change_time = SDL_GetTicks();
                char msg[96];
                snprintf(msg, sizeof(msg), "View shader: %s",
                         pixel_shader_get_name(viewer.view_pixel_shader));
                imgui_layer_set_status_message(msg);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Per-pixel filter baked into the view surface (cycle with P)");

            if (options) {
                int filter_index = (int)options->color_filter;
                if (filter_index < 0 || filter_index >= COLOR_FILTER_COUNT) {
                    filter_index = COLOR_FILTER_NONE;
                }

                static const char *filter_items[] = {
                    "None",
                    "Gray World",
                    "Reduce Warm",
                    "Reduce Cool",
                    "Boost Contrast",
                    "Desaturate",
                    "Binarize"
                };
                
                if (ImGui::Combo("Color Filter", &filter_index, filter_items, IM_ARRAYSIZE(filter_items))) {
                    comic_viewer_finish_loading();
                    options->color_filter = (ColorFilterType)filter_index;
                    comic_viewer_reload_current_view();
                    viewer.last_page_change_time = SDL_GetTicks();
                    imgui_layer_set_status_message(color_filter_get_name((ColorFilterType)filter_index));
                }
            }
        }

        ImGui::SeparatorText("Border Removal");
        {
            bool border_removal_enabled = viewer.border_removal_enabled;
            if (ImGui::Checkbox(IC_("Enable Auto Border Removal"), &border_removal_enabled)) {
                comic_viewer_finish_loading();
                viewer.border_removal_enabled = border_removal_enabled;
                comic_viewer_reload_current_view();
                viewer.last_page_change_time = SDL_GetTicks();
                imgui_layer_set_status_message(border_removal_enabled ?
                    "Border removal enabled" :
                    "Border removal disabled");
            }

            ImGui::BeginDisabled(!viewer.border_removal_enabled);

            int border_white_threshold = viewer.border_white_threshold;
            bool border_threshold_changed = ImGui::SliderInt("White Threshold", &border_white_threshold, 0, 255);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("RGB average above this value is considered background (default: 240)");

            int border_required_non_white = viewer.border_required_non_white;
            bool border_samples_changed = ImGui::SliderInt("Content Samples", &border_required_non_white, 1, 32);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Number of non-background pixels needed to stop scanning (default: 3)");

            if (border_threshold_changed || border_samples_changed) {
                comic_viewer_finish_loading();
                viewer.border_white_threshold = border_white_threshold;
                viewer.border_required_non_white = border_required_non_white;
                comic_viewer_reload_current_view();
                viewer.last_page_change_time = SDL_GetTicks();
                imgui_layer_set_status_message("Border removal thresholds updated");
            }

            if (ImGui::Button(IC_("Reset to Defaults"))) {
                comic_viewer_finish_loading();
                viewer.border_white_threshold = 240;
                viewer.border_required_non_white = 3;
                comic_viewer_reload_current_view();
                viewer.last_page_change_time = SDL_GetTicks();
                imgui_layer_set_status_message("Border removal thresholds reset");
            }

            ImGui::EndDisabled();
        }

        ImGui::SeparatorText("Image Backend");
        {
            int selected = (int)image_loader_get_active_backend();
            if (selected < 0 || selected >= (int)IMAGE_BACKEND_COUNT) {
                selected = (int)IMAGE_BACKEND_AUTO;
            }

            static const char *backend_labels[] = {
                "Auto",
                "FreeImage",
                "SDL_image",
                "Imlib2"
            };

            bool selectable[IMAGE_BACKEND_COUNT] = {true, true, true, true};
            for (int i = 1; i < (int)IMAGE_BACKEND_COUNT; ++i) {
                selectable[i] = image_loader_backend_available((ImageBackend)i);
            }

            static char display_labels[IMAGE_BACKEND_COUNT][48];
            const char *items[IMAGE_BACKEND_COUNT];
            for (int i = 0; i < (int)IMAGE_BACKEND_COUNT; ++i) {
                if (i == (int)IMAGE_BACKEND_AUTO || selectable[i]) {
                    snprintf(display_labels[i], sizeof(display_labels[i]), "%s", backend_labels[i]);
                } else {
                    snprintf(display_labels[i], sizeof(display_labels[i]), "%s [unavailable]", backend_labels[i]);
                }
                items[i] = display_labels[i];
            }

            if (ImGui::Combo("Image Decoder", &selected, items, IMAGE_BACKEND_COUNT)) {
                if (selected == (int)IMAGE_BACKEND_AUTO || selectable[selected]) {
                    comic_viewer_finish_loading();
                    if (image_loader_set_preferred_backend((ImageBackend)selected)) {
                        comic_viewer_reload_current_view();
                        viewer.last_page_change_time = SDL_GetTicks();
                        imgui_layer_set_status_message("Image backend switched");
                    } else {
                        imgui_layer_set_status_message("Failed to switch image backend");
                    }
                } else {
                    imgui_layer_set_status_message("Selected image backend is unavailable");
                }
            }

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               "Active: %s",
                               image_loader_backend_name(image_loader_get_active_backend()));
        }

        ImGui::SeparatorText("PDF Backend");
        {
            int backend_count = pdf_backend_get_count();
            int current = (int)viewer.pdf_backend;
            if (current < 0 || current >= backend_count) current = 0;

            // Build labels with availability info
            const char *labels[PDF_BACKEND_COUNT];
            static char label_bufs[PDF_BACKEND_COUNT][64];
            for (int i = 0; i < backend_count && i < PDF_BACKEND_COUNT; i++) {
                bool avail = pdf_backend_is_available(i);
                snprintf(label_bufs[i], sizeof(label_bufs[i]), "%s%s",
                         pdf_backend_get_name(i), avail ? "" : " [unavailable]");
                labels[i] = label_bufs[i];
            }

            if (ImGui::Combo("PDF Renderer", &current, labels, backend_count)) {
                if (pdf_backend_is_available(current)) {
                    viewer.pdf_backend = (PdfBackendType)current;
                }
            }

            int dpi = viewer.pdf_dpi;
            if (ImGui::SliderInt("DPI", &dpi, 36, 600)) {
                if (dpi < 36)  dpi = 36;
                if (dpi > 600) dpi = 600;
                viewer.pdf_dpi = dpi;
            }

            if (viewer.type == SOURCE_PDF) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                    "Reopen the file to apply a new backend or DPI");
            }
        }

        ImGui::SeparatorText("Zoom / Pan");
        ImGui::Text("Zoomed: %s", viewer.zoomed ? "ON" : "OFF");
        ImGui::Text("Zoom center: (%.0f, %.0f)", viewer.zoom_center_x, viewer.zoom_center_y);
        ImGui::Text("Zoom level: %.2f", viewer.zoom_level);
        ImGui::Text("Pan offset: (%.0f, %.0f)", viewer.pan_offset_x, viewer.pan_offset_y);
        ImGui::Text("Pan velocity: (%.0f, %.0f)", viewer.pan_velocity_x, viewer.pan_velocity_y);

        ImGui::SeparatorText("Threads");
        {
            ImGui::TextUnformatted("Load Thread:");
            ImGui::SameLine();
            if (viewer.load_thread) {
                SDL_ThreadState state = SDL_GetThreadState(viewer.load_thread);
                if (state == SDL_THREAD_ALIVE) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "ALIVE");
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "UNKNOWN");
                }
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "IDLE");
            }
        }
    }
    ImGui::End();
}

void imgui_layer_render(void) {
    if (!g_imgui_initialized) return;

    imgui_layer_build_ui();

    ImGui::Render();
    ImDrawData *draw_data = ImGui::GetDrawData();
    ImVec2 framebuffer_scale = draw_data->FramebufferScale;
    SDL_RendererLogicalPresentation presentation;
    int logical_width, logical_height;
    if (SDL_GetRenderLogicalPresentation(g_renderer, &logical_width, &logical_height, &presentation) &&
        presentation != SDL_LOGICAL_PRESENTATION_DISABLED) {
        draw_data->FramebufferScale = ImVec2(1.0f, 1.0f);
    }
    ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, g_renderer);
    draw_data->FramebufferScale = framebuffer_scale;
}

void imgui_layer_set_visible(bool visible) {
    g_imgui_visible = visible;
}

bool imgui_layer_is_visible(void) {
    return g_imgui_visible;
}

void imgui_layer_toggle_exif_info(void) {
    g_exif_visible = !g_exif_visible;
    if (g_exif_visible) {
        // Force a re-read next frame so freshly toggling reflects the current image.
        g_exif_path[0] = '\0';
        g_exif_count = 0;
    }
}

bool imgui_layer_is_exif_info_visible(void) {
    return g_exif_visible;
}

bool imgui_layer_wants_capture_keyboard(void) {
    if (!g_imgui_initialized) return false;
    const ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

bool imgui_layer_wants_capture_mouse(void) {
    if (!g_imgui_initialized) return false;
    const ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

void imgui_layer_set_status_message(const char *message) {
    if (message) {
        strncpy(g_status_message, message, sizeof(g_status_message) - 1);
        g_status_message[sizeof(g_status_message) - 1] = '\0';
        g_status_message_time = SDL_GetTicks();
    } else {
        g_status_message[0] = '\0';
    }
}
