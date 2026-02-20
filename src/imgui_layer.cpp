#include "imgui_layer.h"

#include <cstdio>
#include <cstring>
#include <SDL3/SDL.h>

#include "comic_viewer.h"
#include "pdf_backend.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

static bool g_imgui_initialized = false;
static bool g_imgui_visible = false;
static SDL_Window *g_window = nullptr;
static SDL_Renderer *g_renderer = nullptr;
static char g_status_message[256] = {0};
static Uint64 g_status_message_time = 0;   // Timestamp when the message was set
static Uint64 g_status_message_duration = 5000; // Auto-dismiss after 5 seconds

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

    if (!g_imgui_visible) return;

    ImGui::SetNextWindowSize(ImVec2(460.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("ic", &g_imgui_visible)) {
        ImGui::TextUnformatted("Dear ImGui overlay (F1 to toggle)");
        ImGui::Separator();
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
            static const char *overlay_items[] = {"Gradient", "Stretched", "Ambilight"};
            int overlay_index = (int)viewer.overlay_mode;
            if (overlay_index < 0) overlay_index = 0;
            if (overlay_index > 2) overlay_index = 2;

            if (ImGui::Combo("Overlay Mode", &overlay_index, overlay_items, IM_ARRAYSIZE(overlay_items))) {
                viewer.overlay_mode = (OverlayMode)overlay_index;
                viewer.last_page_change_time = SDL_GetTicks();
            }
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

            if (viewer.type == SOURCE_PDF) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                    "Reopen the file to apply a new backend");
            }
        }

        ImGui::SeparatorText("Zoom / Pan");
        ImGui::Text("Zoomed: %s", viewer.zoomed ? "ON" : "OFF");
        ImGui::Text("Zoom center: (%.0f, %.0f)", viewer.zoom_center_x, viewer.zoom_center_y);
        ImGui::Text("Zoom level: %.2f", viewer.zoom_level);
        ImGui::Text("Pan offset: (%.0f, %.0f)", viewer.pan_offset_x, viewer.pan_offset_y);
        ImGui::Text("Pan velocity: (%.0f, %.0f)", viewer.pan_velocity_x, viewer.pan_velocity_y);
    }
    ImGui::End();
}

void imgui_layer_render(void) {
    if (!g_imgui_initialized) return;

    imgui_layer_build_ui();

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
}

void imgui_layer_set_visible(bool visible) {
    g_imgui_visible = visible;
}

bool imgui_layer_is_visible(void) {
    return g_imgui_visible;
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
